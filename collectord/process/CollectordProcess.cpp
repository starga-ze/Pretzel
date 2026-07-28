#include "process/CollectordProcess.h"
#include "util/Logger.h"

namespace pz::collectord
{

constexpr int kIpcClientTimeoutMs = 10;

CollectordProcess::CollectordProcess(pz::ipc::IpcClient* ipcClient, CollectordServiceManager* serviceManager,
                           boost::asio::io_context* ioContext)
    : m_ipcClient(ipcClient), m_serviceManager(serviceManager), m_ioContext(ioContext)
{
}

bool CollectordProcess::start()
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

void CollectordProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    // Runs whatever outbound device calls became ready and returns; the ipc poll above already
    // paces the loop, so this never spins. Completion handlers run here, on this thread.
    if (m_ioContext)
    {
        m_ioContext->poll();

        // poll() leaves the context in a stopped state once it runs dry, and a stopped context
        // silently ignores work queued afterwards — so the next test would never start.
        if (m_ioContext->stopped())
        {
            m_ioContext->restart();
        }
    }

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

}
