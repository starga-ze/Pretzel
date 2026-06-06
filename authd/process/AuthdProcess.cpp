#include "process/AuthdProcess.h"
#include "util/Logger.h"

namespace nf::authd
{

constexpr int kIpcClientTimeoutMs = 10;

AuthdProcess::AuthdProcess(nf::ipc::IpcClient* ipcClient,
                           AuthdServiceManager* serviceManager) :
    m_ipcClient(ipcClient),
    m_serviceManager(serviceManager)
{
}

bool AuthdProcess::start()
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

void AuthdProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

} // namespace nf::authd
