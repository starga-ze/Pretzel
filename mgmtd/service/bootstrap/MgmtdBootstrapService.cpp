#include "service/bootstrap/MgmtdBootstrapService.h"

#include "service/MgmtdServiceManager.h"
#include "event/MgmtdEventFactory.h"
#include "action/MgmtdActionFactory.h"
#include "router/MgmtdTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "util/Logger.h"

namespace nf::mgmtd
{

constexpr auto kClientHelloInterval  = std::chrono::seconds(1);
constexpr auto kRuntimeReadyInterval = std::chrono::seconds(1);
constexpr auto kBootstrapTimeout     = std::chrono::seconds(10);

MgmtdBootstrapService::MgmtdBootstrapService(MgmtdEventFactory* eventFactory,
                                             MgmtdActionFactory* actionFactory)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory)
{
}

void MgmtdBootstrapService::start()
{
    m_state = State::Init;
    m_startedAt = std::chrono::steady_clock::now();
    m_lastClientHelloSentAt = {};
    m_lastRuntimeReadySentAt = {};

    LOG_INFO("MgmtdBootstrapService start...");
}

std::unique_ptr<MgmtdEvent>
MgmtdBootstrapService::schedule(std::chrono::steady_clock::time_point now)
{
    if (!m_eventFactory)
    {
        LOG_ERROR("MgmtdBootstrapService: eventFactory is nullptr");
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

        LOG_DEBUG("MgmtdBootstrapService: schedule SendClientHello");

        return m_eventFactory->create(
            MgmtdEventDomain::Bootstrap,
            static_cast<std::uint32_t>(MgmtdBootstrapEventType::SendClientHello));
    }

    case State::WaitServerHello:
    {
        if (checkTimeout(now, "WaitServerHello"))
        {
            return nullptr;
        }

        if (now - m_lastClientHelloSentAt >= kClientHelloInterval)
        {
            m_lastClientHelloSentAt = now;

            LOG_DEBUG("MgmtdBootstrapService: schedule Re-SendClientHello");

            return m_eventFactory->create(
                MgmtdEventDomain::Bootstrap,
                static_cast<std::uint32_t>(MgmtdBootstrapEventType::SendClientHello));
        }

        return nullptr;
    }

    case State::WaitRuntimeStart:
    {
        if (checkTimeout(now, "WaitRuntimeStart"))
        {
            return nullptr;
        }

        if (now - m_lastRuntimeReadySentAt >= kRuntimeReadyInterval)
        {
            m_lastRuntimeReadySentAt = now;

            LOG_DEBUG("MgmtdBootstrapService: schedule Re-SendRuntimeReady");

            return m_eventFactory->create(
                MgmtdEventDomain::Bootstrap,
                static_cast<std::uint32_t>(MgmtdBootstrapEventType::SendRuntimeReady));
        }

        return nullptr;
    }

    case State::Ready:
    {
        m_state = State::Running;
        LOG_INFO("MgmtdBootstrapService: state change to Running");
        return nullptr;
    }

    case State::Running:
    case State::Failed:
        return nullptr;
    }

    return nullptr;
}

bool MgmtdBootstrapService::isReady() const
{
    return m_state == State::Running;
}

void MgmtdBootstrapService::handleEvent(MgmtdServiceManager& serviceManager,
                                        const MgmtdBootstrapEvent& event)
{
    if (!m_actionFactory)
    {
        LOG_ERROR("MgmtdBootstrapService: actionFactory is nullptr");
        return;
    }

    switch (event.type())
    {
    case MgmtdBootstrapEventType::SendClientHello:
    {
        auto action = m_actionFactory->create(
            MgmtdActionDomain::Bootstrap,
            static_cast<std::uint32_t>(MgmtdBootstrapActionType::SendClientHello));

        serviceManager.postAction(std::move(action));
        break;
    }

    case MgmtdBootstrapEventType::ReceiveServerHello:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("MgmtdBootstrapService: ReceiveServerHello has empty message");
            return;
        }

        onServerHello(serviceManager, *msg);
        break;
    }

    case MgmtdBootstrapEventType::SendRuntimeReady:
    {
        auto action = m_actionFactory->create(
            MgmtdActionDomain::Bootstrap,
            static_cast<std::uint32_t>(MgmtdBootstrapActionType::SendRuntimeReady));

        serviceManager.postAction(std::move(action));
        break;
    }

    case MgmtdBootstrapEventType::ReceiveRuntimeStart:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("MgmtdBootstrapService: ReceiveRuntimeStart has empty message");
            return;
        }

        onRuntimeStart(*msg);
        break;
    }

    default:
        LOG_WARN("MgmtdBootstrapService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void MgmtdBootstrapService::handleAction(MgmtdServiceManager& serviceManager,
                                         const MgmtdBootstrapAction& action)
{
    std::unique_ptr<nf::ipc::IpcMessage> msg = nullptr;

    switch (action.type())
    {
    case MgmtdBootstrapActionType::SendClientHello:
    {
        if (m_state != State::WaitServerHello)
        {
            LOG_DEBUG("MgmtdBootstrapService: skip SendClientHello state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("MgmtdBootstrapService: Tx ClientHello, waiting ServerHello");
        msg = buildClientHelloMessage();
        break;
    }

    case MgmtdBootstrapActionType::SendRuntimeReady:
    {
        if (m_state != State::WaitRuntimeStart)
        {
            LOG_DEBUG("MgmtdBootstrapService: skip SendRuntimeReady state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("MgmtdBootstrapService: Tx RuntimeReady, waiting RuntimeStart");
        msg = buildRuntimeReadyMessage();
        break;
    }

    default:
        LOG_WARN("MgmtdBootstrapService: unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void MgmtdBootstrapService::onServerHello(MgmtdServiceManager& serviceManager,
                                          const nf::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitServerHello)
    {
        LOG_WARN("MgmtdBootstrapService: ServerHello in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::WaitRuntimeStart;
    m_lastRuntimeReadySentAt = std::chrono::steady_clock::now();

    LOG_INFO("MgmtdBootstrapService: state change to WaitRuntimeStart");

    auto action = m_actionFactory->create(
        MgmtdActionDomain::Bootstrap,
        static_cast<std::uint32_t>(MgmtdBootstrapActionType::SendRuntimeReady));

    serviceManager.postAction(std::move(action));
}

void MgmtdBootstrapService::onRuntimeStart(const nf::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitRuntimeStart)
    {
        LOG_WARN("MgmtdBootstrapService: RuntimeStart in unexpected state={}",
                 static_cast<int>(m_state));
        return;
    }

    m_state = State::Ready;

    LOG_INFO("MgmtdBootstrapService: state change to Ready");
}

bool MgmtdBootstrapService::checkTimeout(std::chrono::steady_clock::time_point now,
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

    LOG_ERROR("MgmtdBootstrapService: bootstrap timeout state={}", stateName);

    m_state = State::Failed;
    return true;
}

std::unique_ptr<nf::ipc::IpcMessage>
MgmtdBootstrapService::buildClientHelloMessage() const
{
    const std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Mgmtd);

    const auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
        nf::ipc::IpcDaemon::Mgmtd,
        nf::ipc::IpcDaemon::Ipcd,
        nf::ipc::IpcCmd::ClientHello,
        0,
        flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

std::unique_ptr<nf::ipc::IpcMessage>
MgmtdBootstrapService::buildRuntimeReadyMessage() const
{
    const std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Mgmtd);

    const auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
        nf::ipc::IpcDaemon::Mgmtd,
        nf::ipc::IpcDaemon::Ipcd,
        nf::ipc::IpcCmd::RuntimeReady,
        0,
        flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

} // namespace nf::mgmtd
