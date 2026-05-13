#pragma once

#include "router/TxRouter.h"

namespace nf::ipcd
{

class IpcServerHandler;

class IpcdTxRouter : public nf::router::TxRouter
{
public:
    IpcdTxRouter(IpcServerHandler* ipcServerHandler);
    ~IpcdTxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    IpcServerHandler* m_ipcServerHandler;
};

} // namespace nf::ipcd
