"""The socket half of the transport: connect to ipcd, frame in, frame out.

Blocking with a timeout rather than async. ragd handles one retrieval at a time and
spends its life idle; an event loop here would buy nothing and cost a reader's
familiarity with the rest of the daemon.

Reconnect is the normal case, not an error path — ipcd restarts, and a daemon that
exits when its socket drops is a daemon that needs a supervisor to do its job.
"""

import errno
import logging
import socket
import time

from .ipc_protocol import HEADER_SIZE, IPC_MAX_FRAME_SIZE, Header, Message

log = logging.getLogger("inferd.transport")


class IpcClient:
    def __init__(self, socket_path, daemon_id, poll_timeout=1.0, reconnect_delay=2.0):
        self._path = socket_path
        self._daemon_id = daemon_id
        self._poll_timeout = poll_timeout
        self._reconnect_delay = reconnect_delay
        self._sock = None
        self._buf = bytearray()
        self._last_attempt = 0.0

    @property
    def connected(self):
        return self._sock is not None

    def connect(self):
        """Returns True if a connection is up. Rate-limited so a down ipcd does not
        turn the idle loop into a connect storm."""
        if self._sock is not None:
            return True

        now = time.monotonic()
        if now - self._last_attempt < self._reconnect_delay:
            return False
        self._last_attempt = now

        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.settimeout(3.0)
            s.connect(self._path)
            s.settimeout(self._poll_timeout)
            self._sock = s
            self._buf.clear()
            log.info("connected to ipcd at %s", self._path)
            return True
        except OSError as e:
            log.warning("connect to %s failed: %s", self._path, e)
            return False

    def close(self, reason=""):
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
            self._buf.clear()
            if reason:
                log.warning("ipc connection dropped: %s", reason)

    def send(self, msg):
        if self._sock is None:
            return False
        try:
            self._sock.sendall(msg.to_bytes())
            return True
        except OSError as e:
            self.close(f"send failed: {e}")
            return False

    def recv(self):
        """One frame, or None if nothing arrived within the poll timeout.

        The timeout is what makes the caller's loop a sleep rather than a spin: with
        no traffic this blocks for poll_timeout and returns None.
        """
        if self._sock is None:
            return None

        while True:
            frame = self._take_frame()
            if frame is not None:
                return frame

            try:
                chunk = self._sock.recv(65536)
            except socket.timeout:
                return None
            except OSError as e:
                if e.errno == errno.EINTR:
                    return None
                self.close(f"recv failed: {e}")
                return None

            if not chunk:
                self.close("peer closed")
                return None

            self._buf.extend(chunk)

    def _take_frame(self):
        """A stream socket delivers bytes, not messages — a frame can arrive split
        across reads or several to a read, so the buffer is drained by length."""
        if len(self._buf) < HEADER_SIZE:
            return None

        header = Header.unpack(bytes(self._buf[:HEADER_SIZE]))

        # A bogus length would otherwise have us buffer forever waiting for bytes that
        # are never coming. The stream is unrecoverable at that point: resynchronising
        # means guessing where the next header starts.
        if header.payload_len > IPC_MAX_FRAME_SIZE:
            self.close(f"frame too large ({header.payload_len} bytes)")
            return None

        total = HEADER_SIZE + header.payload_len
        if len(self._buf) < total:
            return None

        payload = bytes(self._buf[HEADER_SIZE:total])
        del self._buf[:total]
        return Message(header, payload)
