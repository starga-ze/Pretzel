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
    std::unique_ptr<nf::ipc::IpcMessage> makeServerHello(const nf::ipc::IpcMessage& request);

    IpcServerHandler* m_ipcServerHandler;
};

} // namespace nf::ipcd
