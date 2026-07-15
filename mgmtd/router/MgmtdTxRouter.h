#pragma once

#include "http/HttpHandler.h"
#include "http/HttpMessage.h"
#include "ipc/IpcClientHandler.h"
#include "ipc/IpcMessage.h"
#include "router/TxRouter.h"

namespace pz::mgmtd
{

class MgmtdTxRouter : public pz::router::TxRouter
{
public:
    MgmtdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler, pz::http::HttpHandler* httpHandler);
    ~MgmtdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void handleHttpMessage(pz::http::HttpResponse response, pz::http::SessionId id) override;

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler = nullptr;
    pz::http::HttpHandler* m_httpHandler = nullptr;
};

}
