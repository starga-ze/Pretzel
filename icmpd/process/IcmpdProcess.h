#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/IcmpdServiceManager.h"

#include <chrono>

namespace nf::icmpd
{

class IcmpdProcess : public nf::process::Process
{
public:
    IcmpdProcess(nf::ipc::IpcClient* ipcClientEngine, IcmpdServiceManager* serviceManager);
    ~IcmpdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    nf::ipc::IpcClient* m_ipcClientEngine;
    IcmpdServiceManager* m_serviceManager;
};

} // namespace nf::icmpd
