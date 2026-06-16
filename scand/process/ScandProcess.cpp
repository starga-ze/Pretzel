#include "process/ScandProcess.h"
#include "util/Logger.h"

namespace pz::scand
{

constexpr int kIpcClientTimeoutMs  = 10;
constexpr int kSnmpEngineTimeoutMs = 5;

ScandProcess::ScandProcess(pz::ipc::IpcClient* ipcClient,
                           ScandServiceManager* serviceManager,
                           SnmpEngine* snmpEngine) :
    m_ipcClient(ipcClient),
    m_serviceManager(serviceManager),
    m_snmpEngine(snmpEngine)
{
}

bool ScandProcess::start()
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

void ScandProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    m_snmpEngine->poll(kSnmpEngineTimeoutMs);

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

} // namespace pz::scand
