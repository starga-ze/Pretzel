#include "process/EnginedProcess.h"
#include "util/Logger.h"

namespace nf::engined
{

constexpr auto kClientHelloInterval = std::chrono::seconds(1);
constexpr auto kSyncRequestInterval = std::chrono::seconds(1);
constexpr auto kBootstrapTimeout = std::chrono::seconds(10);
constexpr int kIpcClientTimeoutMs = 10;

EnginedProcess::EnginedProcess(nf::ipc::IpcClient* ipcClient, EnginedTxRouter* txRouter)
    : m_ipcClient(ipcClient), m_txRouter(txRouter)
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
    m_lastClientHelloSentAt = {};
    m_lastSyncRequestSentAt = {};

    return true;
}

void EnginedProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    if (m_bootstrapState != BootstrapState::Running)
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
        if (checkBootstrapTimeout(now, "Init"))
        {
            return;
        }

        m_txRouter->sendClientHello();

        LOG_INFO("Tx ClientHello, waiting ServerHello...");

        m_lastClientHelloSentAt = now;
        m_bootstrapState = BootstrapState::WaitHandshake;

        LOG_INFO("State change to WaitHandshake");
        return;
    }

    case BootstrapState::WaitHandshake:
    {
        if (checkBootstrapTimeout(now, "WaitHandshake"))
        {
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
    {
        if (checkBootstrapTimeout(now, "WaitSync"))
        {
            return;
        }

        if (now - m_lastSyncRequestSentAt >= kSyncRequestInterval)
        {
            m_txRouter->sendSyncRequest();
            m_lastSyncRequestSentAt = now;

            LOG_INFO("Re-Tx SyncRequest, waiting Sync...");
        }

        return;
    }

    case BootstrapState::Ready:
    {
        /* Note that RuntimeRequest does not handle retransmission. */
        m_txRouter->sendRuntimeRequest();

        m_bootstrapState = BootstrapState::Running;

        LOG_INFO("State change to Running");
        return;
    }

    case BootstrapState::Running:
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
        LOG_WARN("ServerHello received in unexpected bootstrap state={}", static_cast<int>(m_bootstrapState));
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    m_txRouter->sendSyncRequest();
    m_lastSyncRequestSentAt = now;

    LOG_INFO("Tx SyncRequest, waiting Sync...");

    m_bootstrapState = BootstrapState::WaitSync;

    LOG_INFO("State change to WaitSync");
}

void EnginedProcess::onSync(const nf::ipc::IpcMessage& msg)
{
    m_bootstrapState = BootstrapState::Ready;

    LOG_INFO("State change to Ready");
}

bool EnginedProcess::checkBootstrapTimeout(std::chrono::steady_clock::time_point now, const char* state)
{
    if (m_bootstrapState == BootstrapState::Failed)
    {
        return true;
    }

    if (now - m_bootstrapStartAt < kBootstrapTimeout)
    {
        return false;
    }
    LOG_ERROR("Bootstrap timeout state={}", state);

    m_bootstrapState = BootstrapState::Failed;
    return true;
}

} // namespace nf::engined
