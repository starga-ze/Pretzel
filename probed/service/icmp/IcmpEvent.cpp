#include "service/icmp/IcmpEvent.h"

#include "service/ProbedServiceManager.h"
#include "service/icmp/IcmpService.h"

#include <utility>

namespace pz::probed
{

IcmpEvent::IcmpEvent(IcmpEventType type) : ProbedEvent(ProbedEventDomain::Icmp), m_type(type)
{
}

IcmpEvent::IcmpEvent(IcmpEventType type, std::string srcIp, std::unique_ptr<IcmpPacket> packet)
    : ProbedEvent(ProbedEventDomain::Icmp), m_type(type), m_srcIp(std::move(srcIp)), m_packet(std::move(packet))
{
}

IcmpEventType IcmpEvent::type() const
{
    return m_type;
}

const std::string& IcmpEvent::srcIp() const
{
    return m_srcIp;
}

const IcmpPacket* IcmpEvent::packet() const
{
    return m_packet.get();
}

void IcmpEvent::dispatch(ProbedServiceManager& serviceManager)
{
    serviceManager.icmpService().handleEvent(serviceManager, *this);
}

}
