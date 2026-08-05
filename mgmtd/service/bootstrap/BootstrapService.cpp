#include "service/bootstrap/BootstrapService.h"

#include "action/MgmtdActionFactory.h"
#include "event/MgmtdEventFactory.h"
#include "router/MgmtdTxRouter.h"
#include "service/MgmtdServiceManager.h"

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include "config/Config.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::mgmtd
{

namespace
{

const nlohmann::json& bootstrapConfig()
{
    return pz::config::Config::serviceSection("mgmtd", "bootstrap");
}

std::chrono::milliseconds clientHelloInterval()
{
    return std::chrono::seconds(bootstrapConfig().value("client_hello_interval_sec", 1));
}

std::chrono::milliseconds bootstrapTimeout()
{
    return std::chrono::seconds(bootstrapConfig().value("bootstrap_timeout_sec", 10));
}

}

BootstrapService::BootstrapService(MgmtdEventFactory* eventFactory, MgmtdActionFactory* actionFactory)
    : m_eventFactory(eventFactory), m_actionFactory(actionFactory)
{
}

void BootstrapService::start()
{
    m_state = State::Init;
    m_startedAt = std::chrono::steady_clock::now();
    m_lastClientHelloSentAt = {};

    LOG_INFO("bootstrap started");
}

std::unique_ptr<MgmtdEvent> BootstrapService::schedule(std::chrono::steady_clock::time_point now)
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

        LOG_DEBUG("scheduling ClientHello");

        return m_eventFactory->create(MgmtdEventDomain::Bootstrap,
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

            LOG_DEBUG("retrying ClientHello");

            return m_eventFactory->create(MgmtdEventDomain::Bootstrap,
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

void BootstrapService::handleEvent(MgmtdServiceManager& serviceManager, const BootstrapEvent& event)
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
        auto action = m_actionFactory->create(MgmtdActionDomain::Bootstrap,
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

    case BootstrapEventType::ReceiveConfigReloadResponse:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("received empty ConfigReloadResponse");
            return;
        }

        onConfigReloadResponse(serviceManager, *msg);
        break;
    }

    default:
        LOG_WARN("unhandled event (type={})", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

// engined answers this whether the fleet converged or not, and the two are not the same outcome.
// The failure carries IpcFlag::Error and {"ok":false}; the browser is holding a progress bar open on
// the result, so reporting a failed reload as "Published" is the one thing this must not do.
//
// The flag is the authority and the payload is a courtesy — a message that lost its body still has a
// header, so a missing payload is read as whatever the flag says rather than as success.
void BootstrapService::onConfigReloadResponse(MgmtdServiceManager& serviceManager,
                                              const pz::ipc::IpcMessage& msg)
{
    bool ok = !pz::ipc::IpcProtocol::hasFlag(msg.getFlags(), pz::ipc::IpcFlag::Error);

    const auto& pl = msg.getPayload();
    if (!pl.empty())
    {
        try
        {
            const auto body = nlohmann::json::parse(std::string(pl.begin(), pl.end()));
            if (body.contains("ok") && body["ok"].is_boolean())
                ok = body["ok"].get<bool>();
        }
        catch (const std::exception& e)
        {
            LOG_WARN("ConfigReloadResponse payload was not JSON ({}) — trusting the header flag", e.what());
        }
    }

    if (ok)
    {
        LOG_INFO("config reload acknowledged by engined");
        serviceManager.completeReload();
        return;
    }

    LOG_ERROR("config reload FAILED — the fleet did not converge onto the committed configuration");
    serviceManager.failReload();
}

void BootstrapService::handleAction(MgmtdServiceManager& serviceManager, const BootstrapAction& action)
{
    std::unique_ptr<pz::ipc::IpcMessage> msg = nullptr;

    switch (action.type())
    {
    case BootstrapActionType::SendClientHello:
    {
        if (m_state != State::WaitServerHello)
        {
            LOG_DEBUG("skip SendClientHello (state={})", static_cast<int>(m_state));
            return;
        }

        LOG_INFO("sent ClientHello, awaiting ServerHello");
        msg = buildClientHelloMessage();
        break;
    }

    default:
        LOG_WARN("unhandled action (type={})", static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void BootstrapService::onServerHello(MgmtdServiceManager& serviceManager, const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    if (m_state != State::WaitServerHello)
    {
        LOG_WARN("ServerHello in unexpected state (state={})", static_cast<int>(m_state));
        return;
    }

    serviceManager.txRouter().handleIpcMessage(buildRuntimeReadyMessage());

    m_state = State::Ready;

    LOG_INFO("sent RuntimeReady; handshake complete (state=Ready)");
}

void BootstrapService::onRuntimeStart(const pz::ipc::IpcMessage& msg)
{
    (void)msg;

    LOG_TRACE("RuntimeStart ignored (mgmtd is not gated on fleet convergence)");
}

bool BootstrapService::checkTimeout(std::chrono::steady_clock::time_point now, const char* stateName)
{
    if (m_state == State::Failed)
    {
        return true;
    }

    if (now - m_startedAt < bootstrapTimeout())
    {
        return false;
    }

    LOG_ERROR("bootstrap timed out (state={})", stateName);

    m_state = State::Failed;
    return true;
}

std::unique_ptr<pz::ipc::IpcMessage> BootstrapService::buildClientHelloMessage() const
{
    const std::string name = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Mgmtd);

    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(pz::ipc::IpcDaemon::Mgmtd, pz::ipc::IpcDaemon::Ipcd,
                                                          pz::ipc::IpcCmd::ClientHello, 0, flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

std::unique_ptr<pz::ipc::IpcMessage> BootstrapService::buildRuntimeReadyMessage() const
{
    const std::string name = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Mgmtd);

    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(pz::ipc::IpcDaemon::Mgmtd, pz::ipc::IpcDaemon::Ipcd,
                                                          pz::ipc::IpcCmd::RuntimeReady, 0, flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

}
