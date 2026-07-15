#include "process/IcmpdProcess.h"
#include "util/Logger.h"

namespace pz::icmpd
{

constexpr int kIpcClientEngineTimeoutMs = 10;
constexpr int kIcmpEngineTimeoutMs = 10;

IcmpdProcess::IcmpdProcess(pz::ipc::IpcClient* ipcClientEngine, IcmpEngine* icmpEngine,
                           IcmpdServiceManager* serviceManager)
    : m_ipcClientEngine(ipcClientEngine), m_icmpEngine(icmpEngine), m_serviceManager(serviceManager)
{
}

bool IcmpdProcess::start()
{
    if (!m_ipcClientEngine)
    {
        LOG_ERROR("IpcClient is not initialized");
        return false;
    }

    if (!m_serviceManager)
    {
        LOG_ERROR("ServiceManager is not initialized");
        return false;
    }

    m_serviceManager->start();

    return true;
}

void IcmpdProcess::tick()
{
    m_ipcClientEngine->poll(kIpcClientEngineTimeoutMs);

    m_icmpEngine->poll(kIcmpEngineTimeoutMs);

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

}
