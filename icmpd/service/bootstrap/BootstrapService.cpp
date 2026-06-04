#include "service/bootstrap/BootstrapService.h"

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

void BootstrapService::handleEvent(const BootstrapEvent& event)
{
    std::unique_ptr<IcmpdAction> action = nullptr;

    switch (event.type())
    {
    case BootstrapEventType::SendClientHello:
    {
        action = m_actionFactory->create(IcmpdActionDomain::Bootstrap,
                                              static_cast<std::uint32_t>(BootstrapActionType::SendClientHello));

        LOG_DEBUG("need to create client hello action");
        break;
    }

    default:
        LOG_WARN("Unhandled bootstrap event type={}", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

} // namespace nf::icmpd
