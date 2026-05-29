#include "process/EnginedProcess.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

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

    initProcessMap();

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
        m_txRouter->sendRuntimeStart();

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
    if (m_bootstrapState != BootstrapState::WaitSync)
    {
        LOG_WARN("Sync received in unexpected bootstrap state={}",
                 static_cast<int>(m_bootstrapState));
        return;
    }

    if (!updateProcessMap(msg))
    {
        LOG_WARN("Synchronization failed, waiting Sync...");
        dumpProcessMap();
        return;
    }

    /* Test */
    if (!isProcessReady(nf::ipc::IpcDaemon::Icmpd))
    {
        LOG_DEBUG("Synchronization incomplete: icmpd is not ready, waiting Sync...");
        dumpProcessMap();
        return;
    }

    LOG_INFO("Synchronization completed: icmpd is ready");
    /* Test */

    LOG_INFO("State change to Ready");
    m_bootstrapState = BootstrapState::Ready;
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

void EnginedProcess::initProcessMap()
{
    m_processMap.clear();

    m_processMap[nf::ipc::IpcDaemon::Ipcd] = true;
    m_processMap[nf::ipc::IpcDaemon::Engined] = true;

    m_processMap[nf::ipc::IpcDaemon::Authd] = false;
    m_processMap[nf::ipc::IpcDaemon::Icmpd] = false;
    m_processMap[nf::ipc::IpcDaemon::Snmpd] = false;
    m_processMap[nf::ipc::IpcDaemon::Topologyd] = false;
    m_processMap[nf::ipc::IpcDaemon::Mgmtd] = false;
}

bool EnginedProcess::updateProcessMap(const nf::ipc::IpcMessage& msg)
{
    const auto& payload = msg.getPayload();
    if (payload.empty())
    {
        LOG_WARN("SyncResponse payload is empty");
        return false;
    }

    try
    {
        const std::string jsonText(
            reinterpret_cast<const char*>(payload.data()),
            payload.size());

        const auto root = nlohmann::json::parse(jsonText);

        if (!root.contains("daemons") || !root["daemons"].is_array())
        {
            LOG_WARN("SyncResponse invalid json: missing daemons array");
            return false;
        }

        for (const auto& item : root["daemons"])
        {
            if (!item.contains("daemon") || !item.contains("ready") ||
                    !item["daemon"].is_string() || !item["ready"].is_boolean())
            {
                LOG_WARN("SyncResponse daemon item invalid: {}", item.dump());
                continue;
            }

            const std::string daemonName = item["daemon"].get<std::string>();
            const bool ready = item["ready"].get<bool>();

            const nf::ipc::IpcDaemon daemon =
                nf::ipc::IpcProtocol::strToDaemon(daemonName);

            auto it = m_processMap.find(daemon);
            if (it == m_processMap.end())
            {
                LOG_DEBUG("Ignore unknown daemon from SyncResponse: {}", daemonName);
                continue;
            }

            it->second = ready;
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("SyncResponse json parse failed: {}", e.what());
        return false;
    }

    return true;
}

bool EnginedProcess::isProcessReady(nf::ipc::IpcDaemon daemon) const
{
    const auto it = m_processMap.find(daemon);
    if (it == m_processMap.end())
    {
        return false;
    }

    return it->second;
}

bool EnginedProcess::isAllProcessReady() const
{
    for (const auto& [daemon, ready] : m_processMap)
    {
        if (!ready)
        {
            return false;
        }
    }

    return true;
}

void EnginedProcess::dumpProcessMap() const
{
    static const std::vector<nf::ipc::IpcDaemon> dumpOrder = {
        nf::ipc::IpcDaemon::Ipcd,
        nf::ipc::IpcDaemon::Engined,
        nf::ipc::IpcDaemon::Authd,
        nf::ipc::IpcDaemon::Icmpd,
        nf::ipc::IpcDaemon::Mgmtd,
        nf::ipc::IpcDaemon::Snmpd,
        nf::ipc::IpcDaemon::Topologyd,
    };

    std::string dump;
    dump += "Process readiness dump:\n";

    for (const auto daemon : dumpOrder)
    {
        const auto it = m_processMap.find(daemon);
        if (it == m_processMap.end())
        {
            continue;
        }

        dump += "  - ";
        dump += nf::ipc::IpcProtocol::daemonToStr(daemon);
        dump += " : ";
        dump += it->second ? "ready" : "not-ready";
        dump += "\n";
    }

    LOG_DEBUG("{}", dump);
}

} // namespace nf::engined
