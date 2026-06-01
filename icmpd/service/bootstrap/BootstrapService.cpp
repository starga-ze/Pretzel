#include "service/bootstrap/BootstrapService.h"

#include "util/Logger.h"

namespace nf::icmpd
{

BootstrapService::BootstrapService()
{
}

void BootstrapService::start()
{
    m_state = State::Init;
    m_startedAt = std::chrono::steady_clock::now();
    
    LOG_INFO("BootstrapService start...");
}

std::unique_ptr<nf::event::Event> BootstrapService::schedule(std::chrono::steady_clock::time_point now)
{
    std::unique_ptr<nf::event::Event> event = nullptr;

    if (m_state == State::Init)
    {
        LOG_DEBUG("Create SendClientHello Event");
        event = std::make_unique<BootstrapEvent>(BootstrapEventType::SendClientHello);

        /* temp implement */
        m_state = State::WaitServerHello;
    }

    return event;
}

bool BootstrapService::isReady()
{
    return false;
}

void BootstrapService::handleEvent(const IcmpdEvent& event)
{
    LOG_INFO("handle event");
}

} // namespace nf::icmpd
