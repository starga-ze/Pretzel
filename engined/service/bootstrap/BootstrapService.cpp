#include "service/bootstrap/BootstrapService.h"

#include "service/EnginedServiceManager.h"
#include "service/commit/CommitEvent.h"
#include "event/EnginedEventFactory.h"
#include "action/EnginedActionFactory.h"
#include "router/EnginedTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "config/Config.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace pz::engined
{

namespace
{

// Defaults match the compiled-in values; overridable via "service"."bootstrap" in
// the running-config (global, merged with section "engined").
const nlohmann::json& bootstrapConfig()
{
    return pz::config::Config::serviceSection("engined", "bootstrap");
}

std::chrono::milliseconds clientHelloInterval()
{
    return std::chrono::seconds(bootstrapConfig().value("client_hello_interval_sec", 1));
}

std::chrono::milliseconds syncRequestInterval()
{
    return std::chrono::seconds(bootstrapConfig().value("sync_request_interval_sec", 1));
}

std::chrono::milliseconds bootstrapTimeout()
{
    return std::chrono::seconds(bootstrapConfig().value("bootstrap_timeout_sec", 10));
}

} // namespace

BootstrapService::BootstrapService(EnginedEventFactory* eventFactory,
                                                 EnginedActionFactory* actionFactory)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory)
{
}

void BootstrapService::start()
{
    m_state = State::Init;
    m_startedAt = std::chrono::steady_clock::now();
    m_lastClientHelloSentAt = {};
    m_lastSyncRequestSentAt = {};

    initProcessMap();

    LOG_INFO("bootstrap started");
}

std::unique_ptr<EnginedEvent>
BootstrapService::schedule(std::chrono::steady_clock::time_point now)
{
    if (!m_eventFactory)
    {
        LOG_ERROR("bootstrap: event factory is not initialized");
        return nullptr;
    }

    switch (m_state)
    {
    case State::Init:
    {
        if (checkTimeout(now, "Init"))
        {
            return nullptr;
        }

        m_state = State::WaitHandshake;
        m_lastClientHelloSentAt = now;

        LOG_DEBUG("bootstrap: scheduling ClientHello");

        return m_eventFactory->create(
            EnginedEventDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapEventType::SendClientHello));
    }

    case State::WaitHandshake:
    {
        if (checkTimeout(now, "WaitHandshake"))
        {
            return nullptr;
        }

        if (now - m_lastClientHelloSentAt >= clientHelloInterval())
        {
            m_lastClientHelloSentAt = now;

            LOG_DEBUG("bootstrap: retrying ClientHello");

            return m_eventFactory->create(
                EnginedEventDomain::Bootstrap,
                static_cast<std::uint32_t>(BootstrapEventType::SendClientHello));
        }

        return nullptr;
    }

    case State::WaitSync:
    {
        if (checkTimeout(now, "WaitSync"))
        {
            return nullptr;
        }

        if (now - m_lastSyncRequestSentAt >= syncRequestInterval())
        {
            m_lastSyncRequestSentAt = now;

            LOG_DEBUG("schedule Re-SendSyncRequest");

            return m_eventFactory->create(
                EnginedEventDomain::Bootstrap,
                static_cast<std::uint32_t>(BootstrapEventType::SendSyncRequest));
        }

        return nullptr;
    }

    case State::Ready:
    {
        m_state = State::Running;
        LOG_INFO("bootstrap: state changed to Running");

        return m_eventFactory->create(
            EnginedEventDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapEventType::SendRuntimeStart));
    }

    case State::Running:
    case State::Failed:
        return nullptr;
    }

    return nullptr;
}

bool BootstrapService::isReady() const
{
    return m_state == State::Running;
}

void BootstrapService::handleEvent(EnginedServiceManager& serviceManager,
                                          const BootstrapEvent& event)
{
    if (!m_actionFactory)
    {
        LOG_ERROR("bootstrap: action factory is not initialized");
        return;
    }

    switch (event.type())
    {
    case BootstrapEventType::SendClientHello:
    {
        auto action = m_actionFactory->create(
            EnginedActionDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapActionType::SendClientHello));

        serviceManager.postAction(std::move(action));
        break;
    }

    case BootstrapEventType::ReceiveServerHello:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("bootstrap: received empty ServerHello");
            return;
        }

        onServerHello(serviceManager, *msg);
        break;
    }

    case BootstrapEventType::SendSyncRequest:
    {
        auto action = m_actionFactory->create(
            EnginedActionDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapActionType::SendSyncRequest));

        serviceManager.postAction(std::move(action));
        break;
    }

    case BootstrapEventType::ReceiveSyncResponse:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("ReceiveSyncResponse has empty message");
            return;
        }

        onSyncResponse(*msg);
        break;
    }

    case BootstrapEventType::SendRuntimeStart:
    {
        auto action = m_actionFactory->create(
            EnginedActionDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapActionType::SendRuntimeStart));

        serviceManager.postAction(std::move(action));
        break;
    }

    case BootstrapEventType::ReceiveRuntimeStart:
    {
        break;
    }

    default:
        LOG_WARN("unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void BootstrapService::handleAction(EnginedServiceManager& serviceManager,
                                           const BootstrapAction& action)
{
    std::unique_ptr<pz::ipc::IpcMessage> msg = nullptr;

    switch (action.type())
    {
    case BootstrapActionType::SendClientHello:
    {
        if (m_state != State::WaitHandshake)
        {
            LOG_DEBUG("skip SendClientHello state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("bootstrap: sent ClientHello, awaiting ServerHello");
        msg = buildClientHelloMessage();
        break;
    }

    case BootstrapActionType::SendSyncRequest:
    {
        if (m_state != State::WaitSync)
        {
            LOG_DEBUG("skip SendSyncRequest state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("Tx SyncRequest, waiting SyncResponse");
        msg = buildSyncRequestMessage();
        break;
    }

    case BootstrapActionType::SendRuntimeStart:
    {
        LOG_INFO("Tx RuntimeStart (broadcast)");
        msg = buildRuntimeStartMessage();
        serviceManager.txRouter().handleIpcMessage(std::move(msg));

        if (m_isReload)
        {
            m_isReload = false;
            LOG_INFO("Tx ConfigReloadResponse to mgmtd");
            serviceManager.txRouter().handleIpcMessage(buildConfigReloadResponse());

            // Notify CommitService that the reload cycle is complete so it can
            // dequeue the next pending task.
            auto doneEvent = serviceManager.eventFactory()->create(
                EnginedEventDomain::Commit,
                static_cast<std::uint32_t>(CommitEventType::ReloadComplete));
            serviceManager.postEvent(std::move(doneEvent));
        }
        return;
    }

    default:
        LOG_WARN("unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void BootstrapService::onServerHello(EnginedServiceManager& serviceManager,
                                            const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitHandshake)
    {
        LOG_WARN("ServerHello in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::WaitSync;
    m_lastSyncRequestSentAt = std::chrono::steady_clock::now();

    LOG_INFO("state change to WaitSync");

    auto action = m_actionFactory->create(
        EnginedActionDomain::Bootstrap,
        static_cast<std::uint32_t>(BootstrapActionType::SendSyncRequest));

    serviceManager.postAction(std::move(action));
}

void BootstrapService::onSyncResponse(const pz::ipc::IpcMessage& msg)
{
    if (m_state != State::WaitSync)
    {
        LOG_WARN("SyncResponse in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    if (!updateProcessMap(msg))
    {
        LOG_WARN("SyncResponse parse failed, waiting Sync...");
        dumpProcessMap();
        return;
    }

    if (!isAllProcessReady())
    {
        LOG_DEBUG("service daemons not all ready, waiting Sync...");
        dumpProcessMap();
        return;
    }

    LOG_INFO("synchronization complete");
    LOG_INFO("bootstrap: state changed to Ready");

    m_state = State::Ready;
}

bool BootstrapService::checkTimeout(std::chrono::steady_clock::time_point now,
                                           const char* stateName)
{
    if (m_state == State::Failed)
    {
        return true;
    }

    if (now - m_startedAt < bootstrapTimeout())
    {
        return false;
    }

    LOG_ERROR("bootstrap: timed out — state={}", stateName);

    m_state = State::Failed;
    return true;
}

void BootstrapService::initProcessMap()
{
    m_processMap.clear();

    // Only the service-layer daemons that engined must wait for.
    // ipcd and mgmtd are infrastructure: always up, never in SyncResponse,
    // and their readiness is not gated here.
    // requiredGeneration=0 at cold-start means any first-connect (gen>=1)
    // is accepted — no prior baseline to compare against.
    m_processMap[pz::ipc::IpcDaemon::Authd]    = {false, 0, 0};
    m_processMap[pz::ipc::IpcDaemon::Icmpd]    = {false, 0, 0};
    m_processMap[pz::ipc::IpcDaemon::Snmpd]    = {false, 0, 0};
    m_processMap[pz::ipc::IpcDaemon::Topologyd]= {false, 0, 0};
}

void BootstrapService::scheduleServiceReload()
{
    // After sending ConfigReload to service daemons, re-enter WaitSync.
    // Set requiredGeneration = knownGeneration + 1 for each daemon so that
    // a daemon still alive from the previous cycle (same generation) is
    // rejected until it has disconnected and reconnected (new generation).
    for (auto& [daemon, ps] : m_processMap)
    {
        ps.ready              = false;
        ps.requiredGeneration = ps.knownGeneration + 1;
        // knownGeneration is preserved — it's the baseline for the "+1"
    }

    m_state = State::WaitSync;
    m_startedAt = std::chrono::steady_clock::now();
    m_lastSyncRequestSentAt = {};

    m_isReload = true;
    LOG_INFO("service-layer reload — re-entering WaitSync");
}

bool BootstrapService::updateProcessMap(const pz::ipc::IpcMessage& msg)
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

        std::set<pz::ipc::IpcDaemon> reported;

        for (const auto& item : root["daemons"])
        {
            if (!item.contains("daemon") || !item.contains("ready") ||
                !item["daemon"].is_string() || !item["ready"].is_boolean())
            {
                LOG_WARN("SyncResponse daemon item invalid: {}", item.dump());
                continue;
            }

            const std::string daemonName = item["daemon"].get<std::string>();
            const bool        ready      = item["ready"].get<bool>();
            const uint32_t    generation = item.value("generation", 0u);
            const pz::ipc::IpcDaemon daemon = pz::ipc::IpcProtocol::strToDaemon(daemonName);

            reported.insert(daemon);

            auto it = m_processMap.find(daemon);
            if (it == m_processMap.end())
            {
                LOG_DEBUG("ignore unknown daemon: {}", daemonName);
                continue;
            }

            ProcessState& ps = it->second;
            ps.knownGeneration = generation;

            if (!ready)
            {
                ps.ready = false;
            }
            else if (generation >= ps.requiredGeneration)
            {
                // ready=true AND fresh enough generation — accept.
                ps.ready = true;
            }
            else
            {
                // ready=true but stale generation: daemon is still alive from
                // the previous cycle; wait for it to disconnect and reconnect.
                ps.ready = false;
                LOG_DEBUG("{} ready but stale gen={} required={}",
                          daemonName, generation, ps.requiredGeneration);
            }
        }

        // Daemons absent from SyncResponse have fd<0 (disconnected from ipcd).
        // Mark not-ready; their generation will increment on next reconnect.
        for (auto& [daemon, ps] : m_processMap)
        {
            if (reported.find(daemon) == reported.end())
            {
                ps.ready = false;
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("SyncResponse json parse failed: {}", e.what());
        return false;
    }

    return true;
}

bool BootstrapService::isProcessReady(pz::ipc::IpcDaemon daemon) const
{
    const auto it = m_processMap.find(daemon);
    if (it == m_processMap.end())
    {
        return false;
    }

    return it->second.ready;
}

bool BootstrapService::isAllProcessReady() const
{
    if (m_processMap.empty())
    {
        LOG_WARN("process map is empty");
        return false;
    }

    bool allReady = true;

    for (const auto& [daemon, ps] : m_processMap)
    {
        if (!ps.ready)
        {
            LOG_WARN("daemon not ready daemon={} gen={} required={}",
                     pz::ipc::IpcProtocol::daemonToStr(daemon),
                     ps.knownGeneration, ps.requiredGeneration);
            allReady = false;
        }
    }

    return allReady;
}

void BootstrapService::dumpProcessMap() const
{
    static const std::vector<pz::ipc::IpcDaemon> dumpOrder = {
        pz::ipc::IpcDaemon::Authd,
        pz::ipc::IpcDaemon::Icmpd,
        pz::ipc::IpcDaemon::Snmpd,
        pz::ipc::IpcDaemon::Topologyd,
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
        dump += pz::ipc::IpcProtocol::daemonToStr(daemon);
        dump += " : ";
        dump += it->second.ready ? "ready" : "not-ready";
        dump += " (gen=";
        dump += std::to_string(it->second.knownGeneration);
        dump += " required=";
        dump += std::to_string(it->second.requiredGeneration);
        dump += ")\n";
    }

    LOG_DEBUG("{}", dump);
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildClientHelloMessage() const
{
    const std::string name = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Engined);

    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Engined,
        pz::ipc::IpcDaemon::Ipcd,
        pz::ipc::IpcCmd::ClientHello,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildSyncRequestMessage() const
{
    const std::string name = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Engined);

    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Engined,
        pz::ipc::IpcDaemon::Ipcd,
        pz::ipc::IpcCmd::SyncRequest,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildRuntimeStartMessage() const
{
    const std::string name = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Engined);

    const auto flag = pz::ipc::IpcProtocol::orFlag(pz::ipc::IpcFlag::Request, pz::ipc::IpcFlag::Broadcast);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Engined,
        pz::ipc::IpcDaemon::Broadcast,
        pz::ipc::IpcCmd::RuntimeStart,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildConfigReloadResponse() const
{
    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Engined,
        pz::ipc::IpcDaemon::Mgmtd,
        pz::ipc::IpcCmd::ConfigReloadResponse,
        0,
        flag);

    return std::make_unique<pz::ipc::IpcMessage>(std::move(header));
}

} // namespace pz::engined
