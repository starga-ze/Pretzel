#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"
#include "ipc/IpcMessage.h"
#include "http/HttpHandler.h"
#include "http/HttpMessage.h"

namespace pz::mgmtd
{

// The egress twin of MgmtdRxRouter: it carries both transports symmetrically. IPC egress
// forwards to IpcClientHandler::egress; HTTP egress forwards to HttpHandler::egress. Both
// handlers are injected at construction (raw, non-owning) exactly like the IPC handler —
// each is owned by its transport (IpcClient / HttpServer) and gets its RxRouter late.
class MgmtdTxRouter : public pz::router::TxRouter
{
public:
    MgmtdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler,
                  pz::http::HttpHandler* httpHandler);
    ~MgmtdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void handleHttpMessage(pz::http::HttpResponse response, pz::http::SessionId id) override;

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler = nullptr;
    pz::http::HttpHandler*     m_httpHandler      = nullptr;
};

} // namespace pz::mgmtd
