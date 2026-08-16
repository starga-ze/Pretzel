"""Inbound dispatch: IpcCmd → handler.

A table rather than an if-chain for the same reason the C++ routers use one — adding a
command should be one line in one place, and an unhandled command should be visible as
a gap in the table rather than as a silently-taken else branch.
"""

import json
import logging

from transport.ipc_protocol import Cmd, Daemon, Flag, Header, Message

log = logging.getLogger("inferd.router")


class RxRouter:
    def __init__(self, retrieval, chat, tx_router):
        self._retrieval = retrieval
        self._chat = chat
        self._tx = tx_router
        self._handlers = {
            Cmd.SERVER_HELLO: self._on_server_hello,
            Cmd.RETRIEVE_REQUEST: self._on_retrieve_request,
            Cmd.CHAT_REQUEST: self._on_chat_request,
        }
        self.registered = False

    def handle(self, msg):
        handler = self._handlers.get(msg.header.cmd)
        if handler is None:
            log.debug("no handler for %s — ignoring", msg.header)
            return
        try:
            handler(msg)
        except Exception:
            # A malformed frame must not take the daemon down with it. inferd is idle
            # almost always; the one thing it owes is to still be here next time.
            log.exception("handler for %s raised", msg.header)

    def _on_server_hello(self, msg):
        self.registered = True
        log.info("registered with ipcd")

    def _on_retrieve_request(self, msg):
        try:
            request = json.loads(msg.payload or b"{}")
        except json.JSONDecodeError:
            payload = {"error": "invalid JSON payload", "code": "BAD_REQUEST"}
        else:
            payload = self._retrieval.retrieve(request)

        # The reply goes back to whoever asked, on the same seqNo — that number is the
        # ticket the browser is polling on, so losing it strands the caller.
        self._tx.send_retrieve_response(
            dst=msg.header.src,
            seq_no=msg.header.seq_no,
            payload=payload,
            is_error="error" in payload,
        )

    def _on_chat_request(self, msg):
        dst, seq = msg.header.src, msg.header.seq_no
        try:
            request = json.loads(msg.payload or b"{}")
        except json.JSONDecodeError:
            return self._tx.send_chat_response(
                dst, seq, {"ok": False, "code": "BAD_REQUEST",
                           "error": "invalid JSON payload"}, is_error=True)

        self._chat.handle_turn(
            request,
            on_retrieval=lambda p: self._tx.send_retrieve_response(dst, seq, p,
                                                                   is_error=not p.get("ok", True)),
            on_answer=lambda p: self._tx.send_chat_response(dst, seq, p,
                                                            is_error=not p.get("ok", False)),
        )
