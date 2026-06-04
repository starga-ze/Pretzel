#pragma once

#include "router/TxRouter.h"

#include "ipc/IpcClientHandler.h"
#include "action/IcmpdAction.h"

namespace nf::icmpd
{

class IcmpdTxRouter : public nf::router::TxRouter
{
public:
    IcmpdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler);
    ~IcmpdTxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
    void handleAction(std::unique_ptr<IcmpdAction> action);

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler;
};

} // namespace nf::icmpd
