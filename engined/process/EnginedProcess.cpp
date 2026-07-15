#include "process/EnginedProcess.h"
#include "util/Logger.h"

namespace pz::engined
{

constexpr int kIpcClientTimeoutMs = 10;

EnginedProcess::EnginedProcess(pz::ipc::IpcClient* ipcClient, EnginedServiceManager* serviceManager)
    : m_ipcClient(ipcClient), m_serviceManager(serviceManager)
{
}

bool EnginedProcess::start()
{
    if (!m_ipcClient)
    {
        LOG_ERROR("IPC client is not initialized");
        return false;
    }

    if (!m_serviceManager)
    {
        LOG_ERROR("service manager is not initialized");
        return false;
    }

    m_serviceManager->start();

    return true;
}

void EnginedProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

}
