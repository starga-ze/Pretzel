#pragma once

#include "ipc/IpcClientHandler.h"
#include "router/TxRouter.h"

namespace pz::authd
{

class AuthdTxRouter : public pz::router::TxRouter
{
public:
    explicit AuthdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler);
    ~AuthdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler;
};

}
