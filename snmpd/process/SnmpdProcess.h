#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/SnmpdServiceManager.h"

#include <chrono>

namespace pz::snmpd
{

class SnmpdProcess : public pz::process::Process
{
public:
    SnmpdProcess(pz::ipc::IpcClient* ipcClient,
                 SnmpdServiceManager* serviceManager);
    ~SnmpdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient;
    SnmpdServiceManager* m_serviceManager;
};

} // namespace pz::snmpd
