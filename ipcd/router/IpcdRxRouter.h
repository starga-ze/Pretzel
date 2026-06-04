#pragma once

#include "router/RxRouter.h"

#include "router/IpcdTxRouter.h"
#include "process/IpcdProcess.h"

namespace nf::ipcd
{

class IpcdRxRouter : public nf::router::RxRouter
{
public:
    IpcdRxRouter(IpcServerHandler* ipcServerHandler, IpcdTxRouter* txRouter);
    ~IpcdRxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

    void setProcess(IpcdProcess* process);

private:
    IpcServerHandler* m_ipcServerHandler = nullptr;
    IpcdTxRouter* m_txRouter = nullptr;
    IpcdProcess* m_process = nullptr;
};

} // namespace nf::ipcd
