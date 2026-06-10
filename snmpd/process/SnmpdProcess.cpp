#include "process/SnmpdProcess.h"
#include "util/Logger.h"

namespace pz::snmpd
{

constexpr int kIpcClientTimeoutMs  = 10;
constexpr int kSnmpEngineTimeoutMs = 5;

SnmpdProcess::SnmpdProcess(pz::ipc::IpcClient* ipcClient,
                           SnmpdServiceManager* serviceManager,
                           SnmpEngine* snmpEngine) :
    m_ipcClient(ipcClient),
    m_serviceManager(serviceManager),
    m_snmpEngine(snmpEngine)
{
}

bool SnmpdProcess::start()
{
    if (!m_ipcClient)
    {
        LOG_ERROR("IpcClient is not initialized");
        return false;
    }

    if (!m_serviceManager)
    {
        LOG_ERROR("ServiceManager is not initialized");
        return false;
    }

    if (!m_snmpEngine)
    {
        LOG_ERROR("SnmpEngine is not initialized");
        return false;
    }

    m_serviceManager->start();

    return true;
}

void SnmpdProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    m_snmpEngine->poll(kSnmpEngineTimeoutMs);

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

} // namespace pz::snmpd
