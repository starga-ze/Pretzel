#include "service/probe/ProbeEvent.h"

#include "service/IcmpdServiceManager.h"
#include "service/probe/ProbeService.h"

#include <utility>

namespace pz::icmpd
{

ProbeEvent::ProbeEvent(ProbeEventType type) : IcmpdEvent(IcmpdEventDomain::Probe), m_type(type)
{
}

ProbeEvent::ProbeEvent(ProbeEventType type, std::string srcIp, std::unique_ptr<IcmpPacket> packet)
    : IcmpdEvent(IcmpdEventDomain::Probe), m_type(type), m_srcIp(std::move(srcIp)), m_packet(std::move(packet))
{
}

ProbeEventType ProbeEvent::type() const
{
    return m_type;
}

const std::string& ProbeEvent::srcIp() const
{
    return m_srcIp;
}

const IcmpPacket* ProbeEvent::packet() const
{
    return m_packet.get();
}

void ProbeEvent::dispatch(IcmpdServiceManager& serviceManager)
{
    serviceManager.probeService().handleEvent(serviceManager, *this);
}

}
