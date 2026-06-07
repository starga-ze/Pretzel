#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"
#include "ipc/IpcMessage.h"

namespace pz::mgmtd
{

class MgmtdTxRouter : public pz::router::TxRouter
{
public:
    explicit MgmtdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler);
    ~MgmtdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler = nullptr;
};

} // namespace pz::mgmtd
