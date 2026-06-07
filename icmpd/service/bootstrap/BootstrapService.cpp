#include "service/bootstrap/BootstrapService.h"

#include "service/IcmpdServiceManager.h"
#include "event/IcmpdEventFactory.h"
#include "action/IcmpdActionFactory.h"
#include "router/IcmpdTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "config/Config.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::icmpd
{

namespace
{

// Defaults match the previous hardcoded values; overridable via
// "tuning"."bootstrap" in running-config.json (section "icmpd").
const nlohmann::json& bootstrapTuning()
{
    return pz::config::Config::tuningSection("icmpd", "bootstrap");
}

std::chrono::milliseconds clientHelloInterval()
{
    return std::chrono::milliseconds(bootstrapTuning().value("client_hello_interval_ms", 1000));
}

std::chrono::milliseconds runtimeReadyInterval()
{
    return std::chrono::milliseconds(bootstrapTuning().value("runtime_ready_interval_ms", 1000));
}

std::chrono::milliseconds bootstrapTimeout()
{
    return std::chrono::milliseconds(bootstrapTuning().value("bootstrap_timeout_ms", 10000));
}

} // namespace

BootstrapService::BootstrapService(IcmpdEventFactory* eventFactory,
                                   IcmpdActionFactory* actionFactory)
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

    LOG_INFO("BootstrapService start...");
}

std::unique_ptr<IcmpdEvent>
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

        m_state = State::WaitServerHello;
        m_lastClientHelloSentAt = now;

        LOG_DEBUG("BootstrapService: schedule SendClientHello");

        return m_eventFactory->create(
            IcmpdEventDomain::Bootstrap,
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

            LOG_DEBUG("BootstrapService: schedule Re-SendClientHello");

            return m_eventFactory->create(
                IcmpdEventDomain::Bootstrap,
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

            LOG_DEBUG("BootstrapService: schedule Re-SendRuntimeReady");

            return m_eventFactory->create(
                IcmpdEventDomain::Bootstrap,
                static_cast<std::uint32_t>(BootstrapEventType::SendRuntimeReady));
        }

        return nullptr;
    }

    case State::Ready:
    {
        m_state = State::Running;
        LOG_INFO("BootstrapService: state change to Running");
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

void BootstrapService::handleEvent(IcmpdServiceManager& serviceManager,
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
            IcmpdActionDomain::Bootstrap,
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

    case BootstrapEventType::SendRuntimeReady:
    {
        auto action = m_actionFactory->create(
            IcmpdActionDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapActionType::SendRuntimeReady));

        serviceManager.postAction(std::move(action));
        break;
    }

    case BootstrapEventType::ReceiveRuntimeStart:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("BootstrapService: ReceiveRuntimeStart has empty message");
            return;
        }

        onRuntimeStart(*msg);
        break;
    }

    default:
        LOG_WARN("BootstrapService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void BootstrapService::handleAction(IcmpdServiceManager& serviceManager,
                                    const BootstrapAction& action)
{
    std::unique_ptr<pz::ipc::IpcMessage> msg = nullptr;

    switch (action.type())
    {
    case BootstrapActionType::SendClientHello:
    {
        if (m_state != State::WaitServerHello)
        {
            LOG_DEBUG("BootstrapService: skip SendClientHello action state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("BootstrapService: Tx ClientHello, waiting ServerHello");
        msg = buildClientHelloMessage();
        break;
    }

    case BootstrapActionType::SendRuntimeReady:
    {
        if (m_state != State::WaitRuntimeStart)
        {
            LOG_DEBUG("BootstrapService: skip SendRuntimeReady action state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("BootstrapService: Tx RuntimeReady, waiting RuntimeStart");
        msg = buildRuntimeReadyMessage();
        break;
    }

    default:
        LOG_WARN("BootstrapService: unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void BootstrapService::onServerHello(IcmpdServiceManager& serviceManager,
                                     const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitServerHello)
    {
        LOG_WARN("ServerHello received in unexpected bootstrap state={}",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::WaitRuntimeStart;
    m_lastRuntimeReadySentAt = std::chrono::steady_clock::now();

    LOG_INFO("BootstrapService: state change to WaitRuntimeStart");

    auto action = m_actionFactory->create(
        IcmpdActionDomain::Bootstrap,
        static_cast<std::uint32_t>(BootstrapActionType::SendRuntimeReady));

    serviceManager.postAction(std::move(action));
}

void BootstrapService::onRuntimeStart(const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitRuntimeStart)
    {
        LOG_WARN("RuntimeStart received in unexpected bootstrap state={}",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::Ready;

    LOG_INFO("BootstrapService: state change to Ready");
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

    LOG_ERROR("BootstrapService: bootstrap timeout state={}", stateName);

    m_state = State::Failed;
    return true;
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildClientHelloMessage() const
{
    std::string name =
        pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Icmpd);

    auto flag =
        pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Icmpd,
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
    std::string name =
        pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Icmpd);

    auto flag =
        pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Icmpd,
        pz::ipc::IpcDaemon::Ipcd,
        pz::ipc::IpcCmd::RuntimeReady,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(
        reinterpret_cast<const std::uint8_t*>(name.data()),
        name.size());

    return msg;
}

} // namespace pz::icmpd
