#include "service/bootstrap/BootstrapService.h"

#include "service/ScandServiceManager.h"
#include "event/ScandEventFactory.h"
#include "action/ScandActionFactory.h"
#include "router/ScandTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "config/Config.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::scand
{

namespace
{

// Defaults match the compiled-in values; overridable via "service"."bootstrap" in
// the running-config (global, merged with section "scand").
const nlohmann::json& bootstrapConfig()
{
    return pz::config::Config::serviceSection("scand", "bootstrap");
}

std::chrono::milliseconds clientHelloInterval()
{
    return std::chrono::seconds(bootstrapConfig().value("client_hello_interval_sec", 1));
}

std::chrono::milliseconds runtimeReadyInterval()
{
    return std::chrono::seconds(bootstrapConfig().value("runtime_ready_interval_sec", 1));
}

std::chrono::milliseconds bootstrapTimeout()
{
    return std::chrono::seconds(bootstrapConfig().value("bootstrap_timeout_sec", 10));
}

} // namespace

BootstrapService::BootstrapService(ScandEventFactory* eventFactory,
                                   ScandActionFactory* actionFactory)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory)
{
}

void BootstrapService::start()
{
    m_state = State::Init;
    m_startedAt = std::chrono::steady_clock::now();
    m_lastClientHelloSentAt = {};
    m_lastRuntimeReadySentAt = {};
    m_bootSlowWarned = false;

    LOG_INFO("bootstrap started");
}

std::unique_ptr<ScandEvent>
BootstrapService::schedule(std::chrono::steady_clock::time_point now)
{
    if (!m_eventFactory)
    {
        LOG_ERROR("event factory is not initialized");
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

        m_state = State::WaitServerHello;
        m_lastClientHelloSentAt = now;

        return m_eventFactory->create(
            ScandEventDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapEventType::SendClientHello));
    }

    case State::WaitServerHello:
    {
        if (checkTimeout(now, "WaitServerHello"))
        {
            return nullptr;
        }

        if (now - m_lastClientHelloSentAt >= clientHelloInterval())
        {
            m_lastClientHelloSentAt = now;

            return m_eventFactory->create(
                ScandEventDomain::Bootstrap,
                static_cast<std::uint32_t>(BootstrapEventType::SendClientHello));
        }

        return nullptr;
    }

    case State::WaitRuntimeStart:
    {
        if (checkTimeout(now, "WaitRuntimeStart"))
        {
            return nullptr;
        }

        if (now - m_lastRuntimeReadySentAt >= runtimeReadyInterval())
        {
            m_lastRuntimeReadySentAt = now;

            return m_eventFactory->create(
                ScandEventDomain::Bootstrap,
                static_cast<std::uint32_t>(BootstrapEventType::SendRuntimeReady));
        }

        return nullptr;
    }

    case State::Ready:
    {
        m_state = State::Running;
        LOG_INFO("bootstrap complete (state=Running)");
        return nullptr;
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

void BootstrapService::handleEvent(ScandServiceManager& serviceManager,
                                   const BootstrapEvent& event)
{
    if (!m_actionFactory)
    {
        LOG_ERROR("action factory is not initialized");
        return;
    }

    switch (event.type())
    {
    case BootstrapEventType::SendClientHello:
    {
        auto action = m_actionFactory->create(
            ScandActionDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapActionType::SendClientHello));

        serviceManager.postAction(std::move(action));
        break;
    }

    case BootstrapEventType::ReceiveServerHello:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("received empty ServerHello");
            return;
        }

        onServerHello(serviceManager, *msg);
        break;
    }

    case BootstrapEventType::SendRuntimeReady:
    {
        auto action = m_actionFactory->create(
            ScandActionDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapActionType::SendRuntimeReady));

        serviceManager.postAction(std::move(action));
        break;
    }

    case BootstrapEventType::ReceiveRuntimeStart:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("received empty RuntimeStart");
            return;
        }

        onRuntimeStart(*msg);
        break;
    }

    default:
        LOG_WARN("unhandled event (type={})",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void BootstrapService::handleAction(ScandServiceManager& serviceManager,
                                    const BootstrapAction& action)
{
    std::unique_ptr<pz::ipc::IpcMessage> msg = nullptr;

    switch (action.type())
    {
    case BootstrapActionType::SendClientHello:
    {
        if (m_state != State::WaitServerHello)
        {
            return;
        }

        LOG_DEBUG("sent ClientHello, awaiting ServerHello");
        msg = buildClientHelloMessage();
        break;
    }

    case BootstrapActionType::SendRuntimeReady:
    {
        if (m_state != State::WaitRuntimeStart)
        {
            return;
        }

        LOG_DEBUG("sent RuntimeReady, awaiting RuntimeStart");
        msg = buildRuntimeReadyMessage();
        break;
    }

    default:
        LOG_WARN("unhandled action (type={})",
                 static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void BootstrapService::onServerHello(ScandServiceManager& serviceManager,
                                     const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitServerHello)
    {
        LOG_WARN("ServerHello received in unexpected bootstrap state (state={})",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::WaitRuntimeStart;
    m_lastRuntimeReadySentAt = std::chrono::steady_clock::now();

    LOG_DEBUG("state changed (state=WaitRuntimeStart)");

    auto action = m_actionFactory->create(
        ScandActionDomain::Bootstrap,
        static_cast<std::uint32_t>(BootstrapActionType::SendRuntimeReady));

    serviceManager.postAction(std::move(action));
}

void BootstrapService::onRuntimeStart(const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    // Only the first RuntimeStart (while in WaitRuntimeStart) advances bootstrap. engined
    // re-broadcasts RuntimeStart to the whole fleet whenever ANY daemon (re)connects (see
    // engined's blessedGeneration), so an already-running daemon keeps receiving it — this
    // is expected, benign steady-state traffic, not an error.
    if (m_state != State::WaitRuntimeStart)
    {
        LOG_TRACE("RuntimeStart ignored (already past handshake, state={})",
                  static_cast<int>(m_state));
        return;
    }

    m_state = State::Ready;

    LOG_DEBUG("state changed (state=Ready)");
}

bool BootstrapService::checkTimeout(std::chrono::steady_clock::time_point now,
                                    const char* stateName)
{
    // Cold boot never gives up: engined may simply be slow to broadcast RuntimeStart
    // (e.g. another service daemon is lagging). Warn once on crossing the threshold,
    // then keep retrying the handshake indefinitely rather than wedging in Failed.
    if (now - m_startedAt >= bootstrapTimeout() && !m_bootSlowWarned)
    {
        m_bootSlowWarned = true;
        LOG_WARN("still waiting on bootstrap, will keep retrying (state={}, waited_s={})",
                 stateName,
                 std::chrono::duration_cast<std::chrono::seconds>(now - m_startedAt).count());
    }
    return false;
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildClientHelloMessage() const
{
    std::string name =
        pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Scand);

    auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Scand,
        pz::ipc::IpcDaemon::Ipcd,
        pz::ipc::IpcCmd::ClientHello,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(
        reinterpret_cast<const std::uint8_t*>(name.data()),
        name.size());

    return msg;
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildRuntimeReadyMessage() const
{
    nlohmann::json payloadJson;
    payloadJson["daemon"]          = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Scand);
    payloadJson["applied_version"] = pz::config::Config::runningConfigVersion();
    const std::string payload = payloadJson.dump();

    auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Scand,
        pz::ipc::IpcDaemon::Ipcd,
        pz::ipc::IpcCmd::RuntimeReady,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(
        reinterpret_cast<const std::uint8_t*>(payload.data()),
        payload.size());

    return msg;
}

} // namespace pz::scand
