#pragma once

#include "process/Process.h"

#include "icmp/IcmpEngine.h"
#include "ipc/IpcClient.h"
#include "service/IcmpdServiceManager.h"

#include <chrono>

namespace pz::icmpd
{

class IcmpdProcess : public pz::process::Process
{
public:
    IcmpdProcess(pz::ipc::IpcClient* ipcClientEngine, IcmpEngine* icmpEngine, IcmpdServiceManager* serviceManager);
    ~IcmpdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClientEngine;
    IcmpEngine* m_icmpEngine;
    IcmpdServiceManager* m_serviceManager;
};

}
