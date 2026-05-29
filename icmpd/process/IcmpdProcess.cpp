#include "process/IcmpdProcess.h"
#include "util/Logger.h"

namespace nf::icmpd
{

constexpr auto kClientHelloInterval = std::chrono::seconds(1);
constexpr auto kRuntimeReadyInterval = std::chrono::seconds(1);
constexpr auto kBootstrapTimeout = std::chrono::seconds(10);
constexpr int kIpcClientTimeoutMs = 10;

IcmpdProcess::IcmpdProcess(nf::ipc::IpcClient* ipcClient, IcmpdTxRouter* txRouter)
    : m_ipcClient(ipcClient), m_txRouter(txRouter)
{
}

bool IcmpdProcess::start()
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
    m_lastRuntimeReadySentAt = {};

    return true;
}

void IcmpdProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    if (m_bootstrapState != BootstrapState::Running)
    {
        processBootstrap();
        return;
    }

    processRuntime();
}

void IcmpdProcess::processBootstrap()
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

    case BootstrapState::WaitRuntime:
    {
        if (checkBootstrapTimeout(now, "WaitRuntime"))
        {
            return;
        }

        if (now - m_lastRuntimeReadySentAt >= kRuntimeReadyInterval)
        {
            m_txRouter->sendRuntimeReady();
            m_lastRuntimeReadySentAt = now;

            LOG_INFO("Re-Tx RuntimeReady, waiting Runtime...");
        }

        return;
    }

    case BootstrapState::Ready:
    {
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

void IcmpdProcess::processRuntime()
{

}

void IcmpdProcess::onServerHello(const nf::ipc::IpcMessage& msg)
{
    if (m_bootstrapState != BootstrapState::WaitHandshake)
    {
        LOG_WARN("ServerHello received in unexpected bootstrap state={}", 
                static_cast<int>(m_bootstrapState));
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    m_txRouter->sendRuntimeReady();
    m_lastRuntimeReadySentAt = now;

    LOG_INFO("Tx RuntimeReady, waiting Runtime...");

    m_bootstrapState = BootstrapState::WaitRuntime;

    LOG_INFO("State change to WaitRuntime...");
}

void IcmpdProcess::onRuntimeStart(const nf::ipc::IpcMessage& msg)
{
    if (m_bootstrapState != BootstrapState::WaitRuntime)
    {
        LOG_WARN("Runtime Request received in unexpected bootstrap state={}", 
                static_cast<int>(m_bootstrapState));
        return;
    }

    LOG_INFO("State change to Ready");
    m_bootstrapState = BootstrapState::Ready;
}

bool IcmpdProcess::checkBootstrapTimeout(std::chrono::steady_clock::time_point now, const char* state)
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
} // namespace nf::icmpd
