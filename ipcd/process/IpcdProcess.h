#pragma once

#include "process/Process.h"

#include "ipc/IpcServer.h"
#include "router/IpcdTxRouter.h"

#include <chrono>

namespace nf::ipcd
{

class IpcdProcess : public nf::process::Process
{
public:
    IpcdProcess(IpcServer* ipcServer, IpcdTxRouter* txRouter);
    ~IpcdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    void processRuntime();

    IpcServer* m_ipcServer;
    IpcdTxRouter* m_txRouter;
};

} // namespace nf::ipcd
