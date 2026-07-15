#include "process/AuthdProcess.h"
#include "util/Logger.h"

namespace pz::authd
{

constexpr int kIpcClientTimeoutMs = 10;

AuthdProcess::AuthdProcess(pz::ipc::IpcClient* ipcClient, AuthdServiceManager* serviceManager)
    : m_ipcClient(ipcClient), m_serviceManager(serviceManager)
{
}

bool AuthdProcess::start()
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

    m_serviceManager->start();

    return true;
}

void AuthdProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

}
