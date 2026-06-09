#include "service/bootstrap/BootstrapService.h"

#include "service/MgmtdServiceManager.h"
#include "event/MgmtdEventFactory.h"
#include "action/MgmtdActionFactory.h"
#include "router/MgmtdTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "config/Config.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::mgmtd
{

namespace
{

// Defaults match the previous hardcoded values; overridable via
// "tuning"."bootstrap" in running-config.json (section "mgmtd").
const nlohmann::json& bootstrapTuning()
{
    return pz::config::Config::tuningSection("mgmtd", "bootstrap");
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

BootstrapService::BootstrapService(MgmtdEventFactory* eventFactory,
                                             MgmtdActionFactory* actionFactory)
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

    LOG_INFO("bootstrap started");
}

std::unique_ptr<MgmtdEvent>
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

        m_state = State::WaitServerHello;
        m_lastClientHelloSentAt = now;

        LOG_DEBUG("bootstrap: scheduling ClientHello");

        return m_eventFactory->create(
            MgmtdEventDomain::Bootstrap,
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

            LOG_DEBUG("bootstrap: retrying ClientHello");

            return m_eventFactory->create(
                MgmtdEventDomain::Bootstrap,
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

            LOG_DEBUG("bootstrap: retrying RuntimeReady");

            return m_eventFactory->create(
                MgmtdEventDomain::Bootstrap,
                static_cast<std::uint32_t>(BootstrapEventType::SendRuntimeReady));
        }

        return nullptr;
    }

    case State::Ready:
    {
        m_state = State::Running;
        LOG_INFO("bootstrap: state changed to Running");
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

void BootstrapService::handleEvent(MgmtdServiceManager& serviceManager,
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
            MgmtdActionDomain::Bootstrap,
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

    case BootstrapEventType::SendRuntimeReady:
    {
        auto action = m_actionFactory->create(
            MgmtdActionDomain::Bootstrap,
            static_cast<std::uint32_t>(BootstrapActionType::SendRuntimeReady));

        serviceManager.postAction(std::move(action));
        break;
    }

    case BootstrapEventType::ReceiveRuntimeStart:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("bootstrap: received empty RuntimeStart");
            return;
        }

        onRuntimeStart(*msg);
        break;
    }

    default:
        LOG_WARN("unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void BootstrapService::handleAction(MgmtdServiceManager& serviceManager,
                                         const BootstrapAction& action)
{
    std::unique_ptr<pz::ipc::IpcMessage> msg = nullptr;

    switch (action.type())
    {
    case BootstrapActionType::SendClientHello:
    {
        if (m_state != State::WaitServerHello)
        {
            LOG_DEBUG("skip SendClientHello state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("bootstrap: sent ClientHello, awaiting ServerHello");
        msg = buildClientHelloMessage();
        break;
    }

    case BootstrapActionType::SendRuntimeReady:
    {
        if (m_state != State::WaitRuntimeStart)
        {
            LOG_DEBUG("skip SendRuntimeReady state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("bootstrap: sent RuntimeReady, awaiting RuntimeStart");
        msg = buildRuntimeReadyMessage();
        break;
    }

    default:
        LOG_WARN("unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void BootstrapService::onServerHello(MgmtdServiceManager& serviceManager,
                                          const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitServerHello)
    {
        LOG_WARN("ServerHello in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::WaitRuntimeStart;
    m_lastRuntimeReadySentAt = std::chrono::steady_clock::now();

    LOG_INFO("bootstrap: state changed to WaitRuntimeStart");

    auto action = m_actionFactory->create(
        MgmtdActionDomain::Bootstrap,
        static_cast<std::uint32_t>(BootstrapActionType::SendRuntimeReady));

    serviceManager.postAction(std::move(action));
}

void BootstrapService::onRuntimeStart(const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitRuntimeStart)
    {
        LOG_WARN("RuntimeStart in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::Ready;

    LOG_INFO("bootstrap: state changed to Ready");
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

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildClientHelloMessage() const
{
    const std::string name = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Mgmtd);

    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Mgmtd,
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
    const std::string name = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Mgmtd);

    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Mgmtd,
        pz::ipc::IpcDaemon::Ipcd,
        pz::ipc::IpcCmd::RuntimeReady,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

} // namespace pz::mgmtd
