#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/SnmpdServiceManager.h"
#include "snmp/SnmpEngine.h"

#include <chrono>

namespace pz::snmpd
{

class SnmpdProcess : public pz::process::Process
{
public:
    SnmpdProcess(pz::ipc::IpcClient* ipcClient,
                 SnmpdServiceManager* serviceManager,
                 SnmpEngine* snmpEngine);
    ~SnmpdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient;
    SnmpdServiceManager* m_serviceManager;
    SnmpEngine* m_snmpEngine;
};

} // namespace pz::snmpd
