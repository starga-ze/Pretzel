#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"
#include "ipc/IpcMessage.h"

namespace pz::apid
{

class ApidTxRouter : public pz::router::TxRouter
{
public:
    explicit ApidTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler);
    ~ApidTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler = nullptr;
};

} // namespace pz::apid
