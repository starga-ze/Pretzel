#pragma once

#include "router/TxRouter.h"

namespace pz::ipcd
{

class IpcServerHandler;

class IpcdTxRouter : public pz::router::TxRouter
{
public:
    explicit IpcdTxRouter(IpcServerHandler* ipcServerHandler);
    ~IpcdTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    IpcServerHandler* m_ipcServerHandler{nullptr};
};

}
