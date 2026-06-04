#include "service/probe/ProbeService.h"

#include "action/IcmpdActionFactory.h"
#include "event/IcmpdEventFactory.h"
#include "router/IcmpdTxRouter.h"
#include "service/IcmpdServiceManager.h"

#include "util/Logger.h"

#include <unistd.h>

namespace nf::icmpd
{

ProbeService::ProbeService(IcmpdEventFactory* eventFactory, IcmpdActionFactory* actionFactory)
    : m_eventFactory(eventFactory), m_actionFactory(actionFactory)
{
}

void ProbeService::start()
{
    m_state = State::Init;

    LOG_INFO("ProbeService start...");
}

std::unique_ptr<IcmpdEvent> ProbeService::schedule(std::chrono::steady_clock::time_point now)
{
    switch (m_state)
    {
    case State::Init:
    case State::NotProbing:
    {
        LOG_DEBUG("Schedule start probing");
        m_state = State::IsProbing;
        return m_eventFactory->create(IcmpdEventDomain::Probe, static_cast<std::uint32_t>(ProbeEventType::StartProbe));
    }

    case State::IsProbing:
    {
        return nullptr;
    }
    }

    return nullptr;
}

void ProbeService::handleEvent(IcmpdServiceManager& serviceManager, const ProbeEvent& event)
{
    if (!m_actionFactory)
    {
        LOG_ERROR("ActionFactory is nullptr");
        return;
    }

    switch (event.type())
    {
    case ProbeEventType::StartProbe:
    {
        auto action =
            m_actionFactory->create(IcmpdActionDomain::Probe, static_cast<std::uint32_t>(ProbeActionType::StartProbe));

        serviceManager.postAction(std::move(action));
        break;
    }

    default:
    {
        LOG_WARN("Unhandled event type={}", static_cast<std::uint32_t>(event.type()));
        break;
    }
    }
}

void ProbeService::handleAction(IcmpdServiceManager& serviceManager,
                                const ProbeAction& action)
{
    std::unique_ptr<IcmpPacket> packet = nullptr;

    switch (action.type())
    {
    case ProbeActionType::StartProbe:
    {
        if (m_state != State::IsProbing)
        {
            LOG_DEBUG("ProbeService: skip StartProbe action state={}",
                      static_cast<int>(m_state));
            return;
        }

        LOG_INFO("ProbeService: Tx ICMP EchoRequest");

        packet = buildEchoRequestPacket();
        break;
    }

    default:
        LOG_WARN("ProbeService: unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        return;
    }

    serviceManager.txRouter().handleIcmpPacket(std::move(packet));
}

std::unique_ptr<IcmpPacket> ProbeService::buildEchoRequestPacket() const
{
    const std::uint16_t identifier =
        static_cast<std::uint16_t>(::getpid() & 0xffff);

    const std::uint16_t sequence = 1;

    const std::string payload = "nf-icmpd-probe";

    return std::make_unique<IcmpPacket>(
        IcmpPacket::buildEchoRequest(
            identifier,
            sequence,
            std::vector<std::uint8_t>(payload.begin(), payload.end())));
}

} // namespace nf::icmpd
