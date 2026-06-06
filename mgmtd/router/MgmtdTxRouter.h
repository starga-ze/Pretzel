#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"
#include "ipc/IpcMessage.h"

namespace nf::mgmtd
{

class MgmtdTxRouter : public nf::router::TxRouter
{
public:
    explicit MgmtdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler);
    ~MgmtdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler = nullptr;
};

} // namespace nf::mgmtd
