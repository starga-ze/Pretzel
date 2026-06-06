#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"

namespace nf::snmpd
{

class SnmpdTxRouter : public nf::router::TxRouter
{
public:
    explicit SnmpdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler);
    ~SnmpdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler;
};

} // namespace nf::snmpd
