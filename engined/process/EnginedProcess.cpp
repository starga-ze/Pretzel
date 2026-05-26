#include "process/EnginedProcess.h"
#include "util/Logger.h"

namespace nf::engined
{

constexpr auto kClientHelloInterval = std::chrono::seconds(1);
constexpr auto kBootstrapTimeout = std::chrono::seconds(10);
constexpr int kIpcClientTimeoutMs = 10;

EnginedProcess::EnginedProcess(nf::ipc::IpcClient* ipcClient, EnginedTxRouter* txRouter) : 
    m_ipcClient(ipcClient), 
    m_txRouter(txRouter)
{
}

bool EnginedProcess::start()
{
    if (!m_ipcClient)
    {
        LOG_ERROR("IpcClient is nullptr");
        return false;
    }

    if (!m_txRouter)
    {
        LOG_ERROR("TxRouter is nullptr");
        return false;
    }

    m_bootstrapState = BootstrapState::Init;
    m_bootstrapStartAt = std::chrono::steady_clock::now();

    return true;
}

void EnginedProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    if (m_bootstrapState != BootstrapState::Ready)
    {
        processBootstrap();
        return;
    }

    processRuntime();
}

void EnginedProcess::processBootstrap()
{
    const auto now = std::chrono::steady_clock::now();

    switch (m_bootstrapState)
    {
    case BootstrapState::Init:
    {
        m_txRouter->sendClientHello();

        m_lastClientHelloSentAt = now;
        m_bootstrapState = BootstrapState::WaitHandshake;

        LOG_INFO("State change to WaitHandshake");
        return;
    }

    case BootstrapState::WaitHandshake:
    {
        if (now - m_bootstrapStartAt >= kBootstrapTimeout)
        {
            LOG_ERROR("Bootstrap timeout while waiting Handshake");
            m_bootstrapState = BootstrapState::Failed;
            return;
        }

        if (now - m_lastClientHelloSentAt >= kClientHelloInterval)
        {
            m_txRouter->sendClientHello();
            m_lastClientHelloSentAt = now;

            LOG_INFO("Re-Tx ClientHello, waiting ServerHello...");
        }

        return;
    }

    case BootstrapState::WaitSync:
        if (now - m_bootstrapStartAt >= kBootstrapTimeout)
        {
            LOG_ERROR("Bootstrap timeout while waiting Sync");
            m_bootstrapState = BootstrapState::Failed;
            return;
        }

        LOG_INFO("Waiting Sync...");
        return;

    case BootstrapState::Ready:
        return;

    case BootstrapState::Failed:
        return;
    }
}

void EnginedProcess::processRuntime()
{

}

void EnginedProcess::onServerHello(const nf::ipc::IpcMessage& msg)
{
    if (m_bootstrapState != BootstrapState::WaitHandshake)
    {
        LOG_WARN("ServerHello received in unexpected bootstrap state={}",
                 static_cast<int>(m_bootstrapState));
        return;
    }

    m_bootstrapState = BootstrapState::WaitSync;

    LOG_INFO("State change to WaitSync");
}

void EnginedProcess::onSync(const nf::ipc::IpcMessage& msg)
{

}

} // namespace nf::engined
