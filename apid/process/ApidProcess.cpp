#include "process/ApidProcess.h"

#include "http/HttpServer.h"
#include "ipc/IpcClient.h"
#include "service/ApidServiceManager.h"
#include "util/Logger.h"

namespace pz::apid
{

namespace
{
constexpr int kIpcClientTimeoutMs = 10;
}

ApidProcess::ApidProcess(pz::ipc::IpcClient* ipcClient,
                         pz::http::HttpServer* httpServer,
                         ApidServiceManager* serviceManager)
    : m_ipcClient(ipcClient),
      m_httpServer(httpServer),
      m_serviceManager(serviceManager)
{
}

bool ApidProcess::start()
{
    if (!m_httpServer)
    {
        LOG_ERROR("HTTP server is not initialized");
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

void ApidProcess::tick()
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

} // namespace pz::apid
