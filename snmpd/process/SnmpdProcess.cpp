#include "process/SnmpdProcess.h"
#include "util/Logger.h"

namespace nf::snmpd
{

constexpr int kIpcClientTimeoutMs = 10;

SnmpdProcess::SnmpdProcess(nf::ipc::IpcClient* ipcClient,
                           SnmpdServiceManager* serviceManager) :
    m_ipcClient(ipcClient),
    m_serviceManager(serviceManager)
{
}

bool SnmpdProcess::start()
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

void SnmpdProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

} // namespace nf::snmpd
