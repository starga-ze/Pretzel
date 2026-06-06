#include "service/bootstrap/BootstrapService.h"

#include "service/EnginedServiceManager.h"
#include "event/EnginedEventFactory.h"
#include "action/EnginedActionFactory.h"
#include "router/EnginedTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>
#include <vector>

namespace nf::engined
{

constexpr auto kClientHelloInterval = std::chrono::seconds(1);
constexpr auto kSyncRequestInterval = std::chrono::seconds(1);
constexpr auto kBootstrapTimeout    = std::chrono::seconds(10);

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

    LOG_INFO("BootstrapService start...");
}

std::unique_ptr<EnginedEvent>
BootstrapService::schedule(std::chrono::steady_clock::time_point now)
{
    if (!m_eventFactory)
    {
        LOG_ERROR("BootstrapService: eventFactory is nullptr");
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

        LOG_DEBUG("BootstrapService: schedule SendClientHello");

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

        if (now - m_lastClientHelloSentAt >= kClientHelloInterval)
        {
            m_lastClientHelloSentAt = now;

            LOG_DEBUG("BootstrapService: schedule Re-SendClientHello");

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

        if (now - m_lastSyncRequestSentAt >= kSyncRequestInterval)
        {
            m_lastSyncRequestSentAt = now;

            LOG_DEBUG("BootstrapService: schedule Re-SendSyncRequest");

            return m_eventFactory->create(
                EnginedEventDomain::Bootstrap,
                static_cast<std::uint32_t>(BootstrapEventType::SendSyncRequest));
        }

        return nullptr;
    }

    case State::Ready:
    {
        m_state = State::Running;
        LOG_INFO("BootstrapService: state change to Running");

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
        LOG_ERROR("BootstrapService: actionFactory is nullptr");
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
            LOG_WARN("BootstrapService: ReceiveServerHello has empty message");
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
            LOG_WARN("BootstrapService: ReceiveSyncResponse has empty message");
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
        LOG_WARN("BootstrapService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void BootstrapService::handleAction(EnginedServiceManager& serviceManager,
                                           const BootstrapAction& action)
{
    std::unique_ptr<nf::ipc::IpcMessage> msg = nullptr;

    switch (action.type())
    {
    case BootstrapActionType::SendClientHello:
    {
        if (m_state != State::WaitHandshake)
        {
            LOG_DEBUG("BootstrapService: skip SendClientHello state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("BootstrapService: Tx ClientHello, waiting ServerHello");
        msg = buildClientHelloMessage();
        break;
    }

    case BootstrapActionType::SendSyncRequest:
    {
        if (m_state != State::WaitSync)
        {
            LOG_DEBUG("BootstrapService: skip SendSyncRequest state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("BootstrapService: Tx SyncRequest, waiting SyncResponse");
        msg = buildSyncRequestMessage();
        break;
    }

    case BootstrapActionType::SendRuntimeStart:
    {
        LOG_INFO("BootstrapService: Tx RuntimeStart (broadcast)");
        msg = buildRuntimeStartMessage();
        break;
    }

    default:
        LOG_WARN("BootstrapService: unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void BootstrapService::onServerHello(EnginedServiceManager& serviceManager,
                                            const nf::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitHandshake)
    {
        LOG_WARN("BootstrapService: ServerHello in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::WaitSync;
    m_lastSyncRequestSentAt = std::chrono::steady_clock::now();

    LOG_INFO("BootstrapService: state change to WaitSync");

    auto action = m_actionFactory->create(
        EnginedActionDomain::Bootstrap,
        static_cast<std::uint32_t>(BootstrapActionType::SendSyncRequest));

    serviceManager.postAction(std::move(action));
}

void BootstrapService::onSyncResponse(const nf::ipc::IpcMessage& msg)
{
    if (m_state != State::WaitSync)
    {
        LOG_WARN("BootstrapService: SyncResponse in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    if (!updateProcessMap(msg))
    {
        LOG_WARN("BootstrapService: SyncResponse parse failed, waiting Sync...");
        dumpProcessMap();
        return;
    }

    if (!isAllProcessReady())
    {
        LOG_DEBUG("BootstrapService: icmpd or mgmtd not ready, waiting Sync...");
        dumpProcessMap();
        return;
    }

    LOG_INFO("BootstrapService: synchronization complete");
    LOG_INFO("BootstrapService: state change to Ready");

    m_state = State::Ready;
}

bool BootstrapService::checkTimeout(std::chrono::steady_clock::time_point now,
                                           const char* stateName)
{
    if (m_state == State::Failed)
    {
        return true;
    }

    if (now - m_startedAt < kBootstrapTimeout)
    {
        return false;
    }

    LOG_ERROR("BootstrapService: bootstrap timeout state={}", stateName);

    m_state = State::Failed;
    return true;
}

void BootstrapService::initProcessMap()
{
    m_processMap.clear();

    m_processMap[nf::ipc::IpcDaemon::Ipcd]     = true;
    m_processMap[nf::ipc::IpcDaemon::Engined]  = true;

    m_processMap[nf::ipc::IpcDaemon::Authd]    = false;
    m_processMap[nf::ipc::IpcDaemon::Icmpd]    = false;
    m_processMap[nf::ipc::IpcDaemon::Snmpd]    = false;
    m_processMap[nf::ipc::IpcDaemon::Topologyd]= false;
    m_processMap[nf::ipc::IpcDaemon::Mgmtd]    = false;
}

bool BootstrapService::updateProcessMap(const nf::ipc::IpcMessage& msg)
{
    const auto& payload = msg.getPayload();
    if (payload.empty())
    {
        LOG_WARN("BootstrapService: SyncResponse payload is empty");
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
            LOG_WARN("BootstrapService: SyncResponse invalid json: missing daemons array");
            return false;
        }

        for (const auto& item : root["daemons"])
        {
            if (!item.contains("daemon") || !item.contains("ready") ||
                !item["daemon"].is_string() || !item["ready"].is_boolean())
            {
                LOG_WARN("BootstrapService: SyncResponse daemon item invalid: {}", item.dump());
                continue;
            }

            const std::string daemonName = item["daemon"].get<std::string>();
            const bool ready = item["ready"].get<bool>();

            const nf::ipc::IpcDaemon daemon = nf::ipc::IpcProtocol::strToDaemon(daemonName);

            auto it = m_processMap.find(daemon);
            if (it == m_processMap.end())
            {
                LOG_DEBUG("BootstrapService: ignore unknown daemon: {}", daemonName);
                continue;
            }

            it->second = ready;
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("BootstrapService: SyncResponse json parse failed: {}", e.what());
        return false;
    }

    return true;
}

bool BootstrapService::isProcessReady(nf::ipc::IpcDaemon daemon) const
{
    const auto it = m_processMap.find(daemon);
    if (it == m_processMap.end())
    {
        return false;
    }

    return it->second;
}

bool BootstrapService::isAllProcessReady() const
{
    bool allReady = true;

    if (m_processMap.empty())
    {
        LOG_WARN("BootstrapService: process map is empty");
        return false;
    }

    for (const auto& [daemon, ready] : m_processMap)
    {
        if (!ready)
        {
            LOG_WARN("BootstrapService: daemon not ready daemon={}",
                     nf::ipc::IpcProtocol::daemonToStr(daemon));

            allReady = false;
        }
    }

    return allReady;
}

void BootstrapService::dumpProcessMap() const
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

std::unique_ptr<nf::ipc::IpcMessage>
BootstrapService::buildClientHelloMessage() const
{
    const std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Engined);

    const auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
        nf::ipc::IpcDaemon::Engined,
        nf::ipc::IpcDaemon::Ipcd,
        nf::ipc::IpcCmd::ClientHello,
        0,
        flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

std::unique_ptr<nf::ipc::IpcMessage>
BootstrapService::buildSyncRequestMessage() const
{
    const std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Engined);

    const auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
        nf::ipc::IpcDaemon::Engined,
        nf::ipc::IpcDaemon::Ipcd,
        nf::ipc::IpcCmd::SyncRequest,
        0,
        flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

std::unique_ptr<nf::ipc::IpcMessage>
BootstrapService::buildRuntimeStartMessage() const
{
    const std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Engined);

    const auto flag = nf::ipc::IpcProtocol::orFlag(nf::ipc::IpcFlag::Request, nf::ipc::IpcFlag::Broadcast);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
        nf::ipc::IpcDaemon::Engined,
        nf::ipc::IpcDaemon::Broadcast,
        nf::ipc::IpcCmd::RuntimeStart,
        0,
        flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

} // namespace nf::engined
