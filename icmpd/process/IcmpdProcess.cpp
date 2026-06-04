#include "process/IcmpdProcess.h"
#include "util/Logger.h"

namespace nf::icmpd
{

constexpr int kIpcClientEngineTimeoutMs = 10;
constexpr int kIcmpEngineTimeoutMs = 10;

IcmpdProcess::IcmpdProcess(nf::ipc::IpcClient* ipcClientEngine, IcmpdServiceManager* serviceManager) : 
    m_ipcClientEngine(ipcClientEngine), m_serviceManager(serviceManager)
{
}

bool IcmpdProcess::start()
{
    if (!m_ipcClientEngine)
    {
        LOG_ERROR("IpcClient is nullptr");
        return false;
    }

    if (!m_serviceManager)
    {
        LOG_ERROR("ServiceManager is nullptr");
        return false;
    }

    m_serviceManager->start();
    
    return true;
}

void IcmpdProcess::tick()
{
    m_ipcClientEngine->poll(kIpcClientEngineTimeoutMs);

    //m_icmpEngine->poll(kIcmpEngineTimeoutMs);

    m_serviceManager->schedule(); 

    m_serviceManager->execute();
}

} // namespace nf::icmpd
