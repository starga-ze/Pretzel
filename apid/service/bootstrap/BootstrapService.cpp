#include "service/bootstrap/BootstrapService.h"

#include "service/ApidServiceManager.h"
#include "event/ApidEventFactory.h"
#include "action/ApidActionFactory.h"
#include "router/ApidTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "config/Config.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::apid
{

namespace
{

// Defaults match the compiled-in values; overridable via "service"."bootstrap" in the
// running-config (global, merged with section "apid").
const nlohmann::json& bootstrapConfig()
{
    return pz::config::Config::serviceSection("apid", "bootstrap");
}

std::chrono::milliseconds clientHelloInterval()
{
    return std::chrono::seconds(bootstrapConfig().value("client_hello_interval_sec", 1));
}

std::chrono::milliseconds bootWarnInterval()
{
    return std::chrono::seconds(bootstrapConfig().value("bootstrap_timeout_sec", 10));
}

} // namespace

BootstrapService::BootstrapService(ApidEventFactory* eventFactory,
                                   ApidActionFactory* actionFactory)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory)
{
}

void BootstrapService::start()
{
    m_state = State::Init;
    m_startedAt = std::chrono::steady_clock::now();
    m_lastClientHelloSentAt = {};
    m_bootSlowWarned = false;

    LOG_INFO("bootstrap started");
}

std::unique_ptr<ApidEvent>
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
        m_state = State::WaitServerHello;
        m_lastClientHelloSentAt = now;

        LOG_DEBUG("scheduling ClientHello");

        return m_eventFactory->create(
            ApidEventDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapEventType::SendClientHello));
    }

    case State::WaitServerHello:
    {
        warnIfBootSlow(now, "WaitServerHello");

        if (now - m_lastClientHelloSentAt >= clientHelloInterval())
        {
            m_lastClientHelloSentAt = now;

            LOG_DEBUG("retrying ClientHello");

            return m_eventFactory->create(
                ApidEventDomain::Bootstrap,
                static_cast<std::uint32_t>(BootstrapEventType::SendClientHello));
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

void BootstrapService::handleEvent(ApidServiceManager& serviceManager,
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
            ApidActionDomain::Bootstrap,
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

void BootstrapService::handleAction(ApidServiceManager& serviceManager,
                                    const BootstrapAction& action)
{
    std::unique_ptr<pz::ipc::IpcMessage> msg = nullptr;

    switch (action.type())
    {
    case BootstrapActionType::SendClientHello:
    {
        if (m_state != State::WaitServerHello)
        {
            LOG_DEBUG("skip SendClientHello (state={})",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("sent ClientHello, awaiting ServerHello");
        msg = buildClientHelloMessage();
        break;
    }

    default:
        LOG_WARN("unhandled action (type={})",
                 static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void BootstrapService::onServerHello(ApidServiceManager& serviceManager,
                                     const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitServerHello)
    {
        LOG_WARN("ServerHello in unexpected state (state={})",
                 static_cast<int>(m_state));
        return;
    }

    // Handshake complete. Report RuntimeReady once — purely informational (lets ipcd /
    // engined observe apid's applied config version); nothing gates on it. apid is
    // infrastructure, so it becomes Ready here rather than waiting for the fleet-wide
    // RuntimeStart it isn't a participant in.
    serviceManager.txRouter().handleIpcMessage(buildRuntimeReadyMessage());

    m_state = State::Ready;

    LOG_INFO("sent RuntimeReady; handshake complete (state=Ready)");
}

void BootstrapService::onRuntimeStart(const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    // apid is infrastructure and is not a config-convergence participant, so the
    // fleet-wide RuntimeStart broadcast carries nothing it needs. It still receives the
    // broadcast (engined re-emits it after every config reload); apid simply ignores it.
    LOG_TRACE("RuntimeStart ignored (apid is not gated on fleet convergence)");
}

void BootstrapService::warnIfBootSlow(std::chrono::steady_clock::time_point now,
                                      const char* stateName)
{
    // Never give up: ipcd/engined may simply be slow to come up. Warn once on crossing
    // the threshold, then keep retrying the handshake indefinitely rather than failing.
    if (now - m_startedAt >= bootWarnInterval() && !m_bootSlowWarned)
    {
        m_bootSlowWarned = true;
        LOG_WARN("still waiting on bootstrap, will keep retrying (state={}, waited_s={})",
                 stateName,
                 std::chrono::duration_cast<std::chrono::seconds>(now - m_startedAt).count());
    }
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildClientHelloMessage() const
{
    const std::string name = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Apid);
    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Apid,
        pz::ipc::IpcDaemon::Ipcd,
        pz::ipc::IpcCmd::ClientHello,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildRuntimeReadyMessage() const
{
    nlohmann::json payloadJson;
    payloadJson["daemon"]          = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Apid);
    payloadJson["applied_version"] = pz::config::Config::runningConfigVersion();
    const std::string payload = payloadJson.dump();

    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Apid,
        pz::ipc::IpcDaemon::Ipcd,
        pz::ipc::IpcCmd::RuntimeReady,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());

    return msg;
}

} // namespace pz::apid
