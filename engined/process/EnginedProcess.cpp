#include "process/EnginedProcess.h"
#include "util/Logger.h"

namespace nf::engined
{

constexpr int kIpcClientTimeoutMs = 10;

EnginedProcess::EnginedProcess(nf::ipc::IpcClient* ipcClient, EnginedServiceManager* serviceManager)
    : m_ipcClient(ipcClient),
      m_serviceManager(serviceManager)
{
}

bool EnginedProcess::start()
{
    if (!m_ipcClient)
    {
        LOG_ERROR("EnginedProcess: IpcClient is nullptr");
        return false;
    }

    if (!m_serviceManager)
    {
        LOG_ERROR("EnginedProcess: ServiceManager is nullptr");
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

} // namespace nf::engined
