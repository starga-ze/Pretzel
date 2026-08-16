"""Outbound: everything inferd puts on the wire is built here.

Kept separate from the transport so message construction is testable without a socket,
and so a reader looking for "what does inferd ever send?" has one file to open.
"""

import json
import logging

from transport.ipc_protocol import Cmd, Daemon, Flag, Header, Message

log = logging.getLogger("inferd.router")


class TxRouter:
    def __init__(self, client):
        self._client = client

    def send_client_hello(self):
        """Registration. The payload is the daemon's own name as a bare string — not
        JSON — matching what the C++ daemons send; ipcd keys its routing table on it."""
        name = b"inferd"
        header = Header(Daemon.INFERD, Daemon.IPCD, Cmd.CLIENT_HELLO,
                        seq_no=0, flags=Flag.REQUEST, payload_len=len(name))
        return self._client.send(Message(header, name))

    def _send_response(self, cmd, dst, seq_no, payload, is_error=False):
        body = json.dumps(payload, ensure_ascii=False).encode()

        # Error rides as a flag on a normal response, not as a separate command: the
        # caller correlates on seqNo and must get exactly one reply per request either
        # way, or its ticket never resolves.
        flags = Flag.RESPONSE | Flag.ERROR if is_error else Flag.RESPONSE

        header = Header(Daemon.INFERD, dst, cmd, seq_no=seq_no, flags=flags,
                        payload_len=len(body))
        if not self._client.send(Message(header, body)):
            log.warning("failed to send %s (seq=%s) — caller will time out", cmd.name, seq_no)
            return False
        return True

    def send_retrieve_response(self, dst, seq_no, payload, is_error=False):
        return self._send_response(Cmd.RETRIEVE_RESPONSE, dst, seq_no, payload, is_error)

    # The second reply on the same ticket. Sent after RetrieveResponse when the turn was
    # grounded, and alone when it was not.
    def send_chat_response(self, dst, seq_no, payload, is_error=False):
        return self._send_response(Cmd.CHAT_RESPONSE, dst, seq_no, payload, is_error)
