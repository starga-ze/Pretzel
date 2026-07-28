#include "process/ProbedProcess.h"
#include "util/Logger.h"

namespace pz::probed
{

constexpr int kIpcClientEngineTimeoutMs = 10;
constexpr int kIcmpEngineTimeoutMs = 10;

ProbedProcess::ProbedProcess(pz::ipc::IpcClient* ipcClientEngine, IcmpEngine* icmpEngine,
                           ProbedServiceManager* serviceManager, boost::asio::io_context* ioContext)
    : m_ipcClientEngine(ipcClientEngine), m_icmpEngine(icmpEngine), m_serviceManager(serviceManager),
      m_ioContext(ioContext)
{
}

bool ProbedProcess::start()
{
    if (!m_ipcClientEngine)
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

void ProbedProcess::tick()
{
    m_ipcClientEngine->poll(kIpcClientEngineTimeoutMs);

    m_icmpEngine->poll(kIcmpEngineTimeoutMs);

    // Runs whatever outbound device calls (connector tests, auto-refresh keygen) became ready and
    // returns; the ipc/icmp polls above already pace the loop, so this never spins. poll() leaves
    // the context stopped once it runs dry, and a stopped context ignores work queued afterwards —
    // so restart it, or the next test would never start.
    if (m_ioContext)
    {
        m_ioContext->poll();

        if (m_ioContext->stopped())
        {
            m_ioContext->restart();
        }
    }

    m_serviceManager->schedule();

    m_serviceManager->execute();
}

}
