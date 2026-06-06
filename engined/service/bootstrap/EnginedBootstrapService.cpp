#include "service/bootstrap/EnginedBootstrapService.h"

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

EnginedBootstrapService::EnginedBootstrapService(EnginedEventFactory* eventFactory,
                                                 EnginedActionFactory* actionFactory)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory)
{
}

void EnginedBootstrapService::start()
{
    m_state = State::Init;
    m_startedAt = std::chrono::steady_clock::now();
    m_lastClientHelloSentAt = {};
    m_lastSyncRequestSentAt = {};

    initProcessMap();

    LOG_INFO("EnginedBootstrapService start...");
}

std::unique_ptr<EnginedEvent>
EnginedBootstrapService::schedule(std::chrono::steady_clock::time_point now)
{
    if (!m_eventFactory)
    {
        LOG_ERROR("EnginedBootstrapService: eventFactory is nullptr");
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

        LOG_DEBUG("EnginedBootstrapService: schedule SendClientHello");

        return m_eventFactory->create(
            EnginedEventDomain::Bootstrap,
            static_cast<std::uint32_t>(EnginedBootstrapEventType::SendClientHello));
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

            LOG_DEBUG("EnginedBootstrapService: schedule Re-SendClientHello");

            return m_eventFactory->create(
                EnginedEventDomain::Bootstrap,
                static_cast<std::uint32_t>(EnginedBootstrapEventType::SendClientHello));
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

            LOG_DEBUG("EnginedBootstrapService: schedule Re-SendSyncRequest");

            return m_eventFactory->create(
                EnginedEventDomain::Bootstrap,
                static_cast<std::uint32_t>(EnginedBootstrapEventType::SendSyncRequest));
        }

        return nullptr;
    }

    case State::Ready:
    {
        m_state = State::Running;
        LOG_INFO("EnginedBootstrapService: state change to Running");

        return m_eventFactory->create(
            EnginedEventDomain::Bootstrap,
            static_cast<std::uint32_t>(EnginedBootstrapEventType::SendRuntimeStart));
    }

    case State::Running:
    case State::Failed:
        return nullptr;
    }

    return nullptr;
}

bool EnginedBootstrapService::isReady() const
{
    return m_state == State::Running;
}

void EnginedBootstrapService::handleEvent(EnginedServiceManager& serviceManager,
                                          const EnginedBootstrapEvent& event)
{
    if (!m_actionFactory)
    {
        LOG_ERROR("EnginedBootstrapService: actionFactory is nullptr");
        return;
    }

    switch (event.type())
    {
    case EnginedBootstrapEventType::SendClientHello:
    {
        auto action = m_actionFactory->create(
            EnginedActionDomain::Bootstrap,
            static_cast<std::uint32_t>(EnginedBootstrapActionType::SendClientHello));

        serviceManager.postAction(std::move(action));
        break;
    }

    case EnginedBootstrapEventType::ReceiveServerHello:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("EnginedBootstrapService: ReceiveServerHello has empty message");
            return;
        }

        onServerHello(serviceManager, *msg);
        break;
    }

    case EnginedBootstrapEventType::SendSyncRequest:
    {
        auto action = m_actionFactory->create(
            EnginedActionDomain::Bootstrap,
            static_cast<std::uint32_t>(EnginedBootstrapActionType::SendSyncRequest));

        serviceManager.postAction(std::move(action));
        break;
    }

    case EnginedBootstrapEventType::ReceiveSyncResponse:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("EnginedBootstrapService: ReceiveSyncResponse has empty message");
            return;
        }

        onSyncResponse(*msg);
        break;
    }

    case EnginedBootstrapEventType::SendRuntimeStart:
    {
        auto action = m_actionFactory->create(
            EnginedActionDomain::Bootstrap,
            static_cast<std::uint32_t>(EnginedBootstrapActionType::SendRuntimeStart));

        serviceManager.postAction(std::move(action));
        break;
    }

    case EnginedBootstrapEventType::ReceiveRuntimeStart:
    {
        break;
    }

    default:
        LOG_WARN("EnginedBootstrapService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void EnginedBootstrapService::handleAction(EnginedServiceManager& serviceManager,
                                           const EnginedBootstrapAction& action)
{
    std::unique_ptr<nf::ipc::IpcMessage> msg = nullptr;

    switch (action.type())
    {
    case EnginedBootstrapActionType::SendClientHello:
    {
        if (m_state != State::WaitHandshake)
        {
            LOG_DEBUG("EnginedBootstrapService: skip SendClientHello state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("EnginedBootstrapService: Tx ClientHello, waiting ServerHello");
        msg = buildClientHelloMessage();
        break;
    }

    case EnginedBootstrapActionType::SendSyncRequest:
    {
        if (m_state != State::WaitSync)
        {
            LOG_DEBUG("EnginedBootstrapService: skip SendSyncRequest state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("EnginedBootstrapService: Tx SyncRequest, waiting SyncResponse");
        msg = buildSyncRequestMessage();
        break;
    }

    case EnginedBootstrapActionType::SendRuntimeStart:
    {
        LOG_INFO("EnginedBootstrapService: Tx RuntimeStart (broadcast)");
        msg = buildRuntimeStartMessage();
        break;
    }

    default:
        LOG_WARN("EnginedBootstrapService: unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void EnginedBootstrapService::onServerHello(EnginedServiceManager& serviceManager,
                                            const nf::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitHandshake)
    {
        LOG_WARN("EnginedBootstrapService: ServerHello in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::WaitSync;
    m_lastSyncRequestSentAt = std::chrono::steady_clock::now();

    LOG_INFO("EnginedBootstrapService: state change to WaitSync");

    auto action = m_actionFactory->create(
        EnginedActionDomain::Bootstrap,
        static_cast<std::uint32_t>(EnginedBootstrapActionType::SendSyncRequest));

    serviceManager.postAction(std::move(action));
}

void EnginedBootstrapService::onSyncResponse(const nf::ipc::IpcMessage& msg)
{
    if (m_state != State::WaitSync)
    {
        LOG_WARN("EnginedBootstrapService: SyncResponse in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    if (!updateProcessMap(msg))
    {
        LOG_WARN("EnginedBootstrapService: SyncResponse parse failed, waiting Sync...");
        dumpProcessMap();
        return;
    }

    if (!isProcessReady(nf::ipc::IpcDaemon::Icmpd) and !isProcessReady(nf::ipc::IpcDaemon::Mgmtd))
    {
        LOG_DEBUG("EnginedBootstrapService: icmpd or mgmtd not ready, waiting Sync...");
        dumpProcessMap();
        return;
    }

    LOG_INFO("EnginedBootstrapService: synchronization complete");
    LOG_INFO("EnginedBootstrapService: state change to Ready");

    m_state = State::Ready;
}

bool EnginedBootstrapService::checkTimeout(std::chrono::steady_clock::time_point now,
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

    LOG_ERROR("EnginedBootstrapService: bootstrap timeout state={}", stateName);

    m_state = State::Failed;
    return true;
}

void EnginedBootstrapService::initProcessMap()
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

bool EnginedBootstrapService::updateProcessMap(const nf::ipc::IpcMessage& msg)
{
    const auto& payload = msg.getPayload();
    if (payload.empty())
    {
        LOG_WARN("EnginedBootstrapService: SyncResponse payload is empty");
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
            LOG_WARN("EnginedBootstrapService: SyncResponse invalid json: missing daemons array");
            return false;
        }

        for (const auto& item : root["daemons"])
        {
            if (!item.contains("daemon") || !item.contains("ready") ||
                !item["daemon"].is_string() || !item["ready"].is_boolean())
            {
                LOG_WARN("EnginedBootstrapService: SyncResponse daemon item invalid: {}", item.dump());
                continue;
            }

            const std::string daemonName = item["daemon"].get<std::string>();
            const bool ready = item["ready"].get<bool>();

            const nf::ipc::IpcDaemon daemon = nf::ipc::IpcProtocol::strToDaemon(daemonName);

            auto it = m_processMap.find(daemon);
            if (it == m_processMap.end())
            {
                LOG_DEBUG("EnginedBootstrapService: ignore unknown daemon: {}", daemonName);
                continue;
            }

            it->second = ready;
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("EnginedBootstrapService: SyncResponse json parse failed: {}", e.what());
        return false;
    }

    return true;
}

bool EnginedBootstrapService::isProcessReady(nf::ipc::IpcDaemon daemon) const
{
    const auto it = m_processMap.find(daemon);
    if (it == m_processMap.end())
    {
        return false;
    }

    return it->second;
}

bool EnginedBootstrapService::isAllProcessReady() const
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

void EnginedBootstrapService::dumpProcessMap() const
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
EnginedBootstrapService::buildClientHelloMessage() const
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
EnginedBootstrapService::buildSyncRequestMessage() const
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
EnginedBootstrapService::buildRuntimeStartMessage() const
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
