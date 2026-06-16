#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/ScandServiceManager.h"
#include "snmp/SnmpEngine.h"

#include <chrono>

namespace pz::scand
{

class ScandProcess : public pz::process::Process
{
public:
    ScandProcess(pz::ipc::IpcClient* ipcClient,
                 ScandServiceManager* serviceManager,
                 SnmpEngine* snmpEngine);
    ~ScandProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient;
    ScandServiceManager* m_serviceManager;
    SnmpEngine* m_snmpEngine;
};

} // namespace pz::scand
