#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"

namespace nf::authd
{

class AuthdTxRouter : public nf::router::TxRouter
{
public:
    explicit AuthdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler);
    ~AuthdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler;
};

} // namespace nf::authd
