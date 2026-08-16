#!/usr/bin/env python3
"""pz-inferd — corpus retrieval for the AI Assistant.

The tenth daemon. It exists because turning a question into a vector needs the
embedding model, and that is Python; the rest of retrieval (pgvector, ranking) a C++
daemon could do, but the embedding decides where the service lives.

Layered like the C++ daemons — core / process / transport / router / service — so that
someone who can read pz-mgmtd can read this. The layers are the same map; the material
is idiomatic Python, and there are no abstract bases with one implementation.

Corpus construction (crawl, chunk, embed) is NOT here. Those are batch jobs that run to
completion and exit, and they live in prisma-rag. inferd only reads what they produced.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from core.inferd_core import InferdCore
from process.inferd_process import (PidFile, install_signal_handlers, load_config,
                                    set_process_title, setup_logging)
from router.rx_router import RxRouter
from router.tx_router import TxRouter
from service.chat_service import ChatService
from service.gateway_service import GatewayService
from service.retrieval_service import RetrievalService
from transport.ipc_client import IpcClient
from transport.ipc_protocol import Daemon, assert_header_size


def main():
    ap = argparse.ArgumentParser(prog="pz-inferd")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    # Cheap, and it catches the one kind of drift from shared/ipc/IpcProtocol.h that a
    # mirrored protocol can catch on its own.
    assert_header_size()

    config = load_config()
    log = setup_logging(config, args.verbose)
    set_process_title()
    log.info("pz-inferd starting")

    ipc_cfg = config["service"]["ipc"]
    client = IpcClient(ipc_cfg["socket_path"], Daemon.INFERD,
                       poll_timeout=ipc_cfg.get("poll_timeout_sec", 1.0))

    retrieval = RetrievalService(config["service"]["retrieval"])
    gateway = GatewayService(config["service"]["gateway"])
    chat = ChatService(retrieval, gateway)

    tx = TxRouter(client)
    rx = RxRouter(retrieval, chat, tx)
    core = InferdCore(client, tx, rx, retrieval,
                    tick_sec=ipc_cfg.get("poll_timeout_sec", 1.0))

    install_signal_handlers(core)

    with PidFile("inferd"):
        try:
            core.run()
        except Exception:
            log.exception("inferd terminated by an unhandled exception")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
