#include "process/TopologydProcess.h"
#include "util/Logger.h"

namespace pz::topologyd
{

constexpr int kIpcClientTimeoutMs = 10;

TopologydProcess::TopologydProcess(pz::ipc::IpcClient* ipcClient,
                                   TopologydServiceManager* serviceManager)
    : m_ipcClient(ipcClient),
      m_serviceManager(serviceManager)
{
}

bool TopologydProcess::start()
{
    if (!m_ipcClient)
    {
        LOG_ERROR("TopologydProcess: IpcClient is nullptr");
        return false;
    }

    if (!m_serviceManager)
    {
        LOG_ERROR("TopologydProcess: ServiceManager is nullptr");
        return false;
    }

    m_serviceManager->start();

    return true;
}

void TopologydProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

} // namespace pz::topologyd
