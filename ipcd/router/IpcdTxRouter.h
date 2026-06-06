#pragma once

#include "router/TxRouter.h"

namespace nf::ipcd
{

class IpcServerHandler;

class IpcdTxRouter : public nf::router::TxRouter
{
public:
    explicit IpcdTxRouter(IpcServerHandler* ipcServerHandler);
    ~IpcdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    IpcServerHandler* m_ipcServerHandler{nullptr};
};

} // namespace nf::ipcd
