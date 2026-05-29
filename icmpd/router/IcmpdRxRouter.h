#pragma once

#include "router/RxRouter.h"

#include "process/IcmpdProcess.h"
#include "router/IcmpdTxRouter.h"

namespace nf::icmpd
{

class IcmpdRxRouter : public nf::router::RxRouter
{
public:
    IcmpdRxRouter(nf::ipc::IpcClientHandler* ipcClientHandler, IcmpdTxRouter* txRouter);
    ~IcmpdRxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

    void setProcess(IcmpdProcess* process);

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler = nullptr;
    IcmpdProcess* m_process = nullptr;
    IcmpdTxRouter* m_txRouter = nullptr;
};

} // namespace nf::icmpd
