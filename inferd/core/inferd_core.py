"""The run loop and the bootstrap state machine.

The loop is the sleep-based idle loop the other daemons run, expressed the way a
blocking socket makes natural: `recv()` carries the poll timeout, so with no traffic
the process blocks in the kernel for a tick and wakes with nothing to do. No spin, no
busy-wait, and no separate timer thread.

Bootstrap mirrors the C++ BootstrapService: connect, say hello, keep saying hello until
ipcd answers. Registration is re-driven on every reconnect rather than only at startup,
because ipcd restarting is normal and a daemon that only registers once goes quietly
deaf when it happens.
"""

import logging
import time

from process.inferd_process import milestone

log = logging.getLogger("inferd.core")

HELLO_RETRY_SEC = 3.0


class InferdCore:
    def __init__(self, client, tx_router, rx_router, retrieval, tick_sec=1.0):
        self._client = client
        self._tx = tx_router
        self._rx = rx_router
        self._retrieval = retrieval
        self._tick = tick_sec
        self._running = False
        self._last_hello = 0.0

    def stop(self, signame=""):
        if self._running:
            log.info("stopping%s", f" on {signame}" if signame else "")
        self._running = False

    def run(self):
        # Kicks off the encoder warm-up in the background and returns at once, so the
        # loop below registers with ipcd immediately rather than 10s from now.
        self._retrieval.start()
        self._running = True
        milestone(log, "inferd running (retrieval warming up in the background)")

        while self._running:
            if not self._client.connected:
                self._rx.registered = False
                if not self._client.connect():
                    # connect() is rate-limited internally; sleeping the tick here keeps
                    # a down ipcd from turning this into a hot loop.
                    time.sleep(self._tick)
                    continue
                self._last_hello = 0.0

            self._maybe_say_hello()

            # Blocks for the poll timeout when idle — this is the sleep.
            msg = self._client.recv()
            if msg is not None:
                self._rx.handle(msg)

        self._client.close()
        milestone(log, "inferd stopped")

    def _maybe_say_hello(self):
        if self._rx.registered:
            return
        now = time.monotonic()
        if now - self._last_hello < HELLO_RETRY_SEC:
            return
        self._last_hello = now
        if self._tx.send_client_hello():
            log.debug("ClientHello sent, waiting for ServerHello")
