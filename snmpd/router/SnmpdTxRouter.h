#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"

namespace pz::snmpd
{

class SnmpdTxRouter : public pz::router::TxRouter
{
public:
    explicit SnmpdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler);
    ~SnmpdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler;
};

} // namespace pz::snmpd
