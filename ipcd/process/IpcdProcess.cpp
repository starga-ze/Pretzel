#include "process/IpcdProcess.h"
#include "util/Logger.h"

namespace pz::ipcd
{

constexpr int kIpcServerTimeoutMs = 10;

IpcdProcess::IpcdProcess(IpcServer* ipcServer, IpcdServiceManager* serviceManager)
    : m_ipcServer(ipcServer),
      m_serviceManager(serviceManager)
{
}

bool IpcdProcess::start()
{
    if (!m_ipcServer)
    {
        LOG_ERROR("IpcdProcess: IpcServer is nullptr");
        return false;
    }

    if (!m_serviceManager)
    {
        LOG_ERROR("IpcdProcess: ServiceManager is nullptr");
        return false;
    }

    m_serviceManager->start();

    return true;
}

void IpcdProcess::tick()
{
    m_ipcServer->poll(kIpcServerTimeoutMs);

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

} // namespace pz::ipcd
