#include "service/bootstrap/BootstrapService.h"
#include "service/IcmpdServiceManager.h"

#include "util/Logger.h"

namespace nf::icmpd
{

BootstrapService::BootstrapService(IcmpdEventFactory* eventFactory, IcmpdActionFactory* actionFactory) : 
    m_eventFactory(eventFactory), 
    m_actionFactory(actionFactory)
{
}

void BootstrapService::start()
{
    m_state = State::Init;
    m_startedAt = std::chrono::steady_clock::now();

    LOG_INFO("BootstrapService start...");
}

std::unique_ptr<IcmpdEvent> BootstrapService::schedule(std::chrono::steady_clock::time_point now)
{
    std::unique_ptr<IcmpdEvent> event = nullptr;

    if (m_state == State::Init)
    {
        LOG_DEBUG("Create SendClientHello Event");
        event = m_eventFactory->create(IcmpdEventDomain::Bootstrap,
                                       static_cast<std::uint32_t>(BootstrapEventType::SendClientHello));

        /* temp implement */
        m_state = State::WaitServerHello;
    }

    return event;
}

bool BootstrapService::isReady()
{
    return false;
}

void BootstrapService::handleEvent(IcmpdServiceManager& serviceManager, const BootstrapEvent& event)
{
    LOG_DEBUG("handleEvent");
    std::unique_ptr<IcmpdAction> action = nullptr;

    switch (event.type())
    {
    case BootstrapEventType::SendClientHello:
    {
        action = m_actionFactory->create(IcmpdActionDomain::Bootstrap,
                                              static_cast<std::uint32_t>(BootstrapActionType::SendClientHello));

        serviceManager.postAction(std::move(action));
        break;
    }

    default:
        LOG_WARN("Unhandled bootstrap event type={}", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void BootstrapService::handleAction(IcmpdServiceManager& serviceManager,
                                    const BootstrapAction& action)
{
    switch (action.type())
    {
    case BootstrapActionType::SendClientHello:
    {
        std::string name =
            nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Icmpd);

        auto flag =
            nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

        nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Icmpd,
            nf::ipc::IpcDaemon::Ipcd,
            nf::ipc::IpcCmd::ClientHello,
            0,
            flag);

        auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
        msg->setPayload(
            reinterpret_cast<const std::uint8_t*>(name.data()),
            name.size());

        serviceManager.txRouter().handleMessage(std::move(msg));
        break;
    }

    default:
        LOG_WARN("Unhandled bootstrap action type={}",
                 static_cast<std::uint32_t>(action.type()));
        break;
    }
}

} // namespace nf::icmpd
