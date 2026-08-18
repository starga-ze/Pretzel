#include "process/MgmtdProcess.h"

#include "grpc/GrpcClientHandler.h"
#include "http/HttpServer.h"
#include "ipc/IpcClient.h"
#include "service/MgmtdServiceManager.h"
#include "util/Logger.h"

namespace pz::mgmtd
{

namespace
{
constexpr int kIpcClientTimeoutMs = 10;
}

MgmtdProcess::MgmtdProcess(pz::ipc::IpcClient* ipcClient, pz::http::HttpServer* httpServer,
                           GrpcClientHandler* grpcClient, MgmtdServiceManager* serviceManager)
    : m_ipcClient(ipcClient), m_httpServer(httpServer), m_grpcClient(grpcClient),
      m_serviceManager(serviceManager)
{
}

bool MgmtdProcess::start()
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

    // The pretzel-ai gRPC transport, pumped the same way as IPC and HTTP: any chat turn a worker
    // finished since the last tick is delivered to the ServiceManager here, on this thread.
    if (m_grpcClient)
    {
        m_grpcClient->poll();
    }

    if (m_serviceManager)
    {
        m_serviceManager->schedule();
        m_serviceManager->execute();
    }
}

}
