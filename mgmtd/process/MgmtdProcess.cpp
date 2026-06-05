#include "process/MgmtdProcess.h"

#include "http/HttpServer.h"
#include "ipc/IpcClient.h"
#include "service/MgmtdServiceManager.h"
#include "util/Logger.h"

namespace nf::mgmtd
{

namespace
{
constexpr int kIpcClientTimeoutMs = 10;
}

MgmtdProcess::MgmtdProcess(nf::ipc::IpcClient* ipcClient,
                           HttpServer* httpServer,
                           MgmtdServiceManager* serviceManager)
    : m_ipcClient(ipcClient),
      m_httpServer(httpServer),
      m_serviceManager(serviceManager)
{
}

bool MgmtdProcess::start()
{
    if (!m_httpServer)
    {
        LOG_ERROR("MgmtdProcess: HttpServer is nullptr");
        return false;
    }

    if (!m_serviceManager)
    {
        LOG_ERROR("MgmtdProcess: ServiceManager is nullptr");
        return false;
    }

    m_serviceManager->start();
    return true;
}

void MgmtdProcess::tick()
{
    if (m_ipcClient)
    {
        m_ipcClient->poll(kIpcClientTimeoutMs);
    }

    if (m_httpServer)
    {
        m_httpServer->poll();
    }

    if (m_serviceManager)
    {
        m_serviceManager->schedule();
        m_serviceManager->execute();
    }
}

} // namespace nf::mgmtd
