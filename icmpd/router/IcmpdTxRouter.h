#pragma once

#include "router/TxRouter.h"

namespace nf::ipc
{
class IpcClientHandler;
}

namespace nf::icmpd
{

class IcmpdTxRouter : public nf::router::TxRouter
{
public:
    IcmpdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler);
    ~IcmpdTxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

    void sendClientHello();
    void sendRuntimeReady();

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler;
};

} // namespace nf::icmpd
