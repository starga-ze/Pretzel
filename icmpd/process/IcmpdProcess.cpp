#include "process/IcmpdProcess.h"
#include "util/Logger.h"

namespace nf::icmpd
{

constexpr int kIpcClientTimeoutMs = 10;
constexpr int kIcmpClientTimeoutMs = 10;

IcmpdProcess::IcmpdProcess(nf::ipc::IpcClient* ipcClient, IcmpdServiceManager* serviceManager) : 
    m_ipcClient(ipcClient), m_serviceManager(serviceManager)
{
}

bool IcmpdProcess::start()
{
    if (!m_ipcClient)
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
    m_ipcClient->poll(kIpcClientTimeoutMs);

    //m_icmpClient->poll(kIcmpClientTimeoutMs);

    m_serviceManager->schedule(); 

    m_serviceManager->execute();
}

} // namespace nf::icmpd
