#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/SnmpdServiceManager.h"

#include <chrono>

namespace nf::snmpd
{

class SnmpdProcess : public nf::process::Process
{
public:
    SnmpdProcess(nf::ipc::IpcClient* ipcClient,
                 SnmpdServiceManager* serviceManager);
    ~SnmpdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    nf::ipc::IpcClient* m_ipcClient;
    SnmpdServiceManager* m_serviceManager;
};

} // namespace nf::snmpd
