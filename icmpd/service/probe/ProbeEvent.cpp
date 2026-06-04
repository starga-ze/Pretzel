#include "service/IcmpdServiceManager.h"
#include "service/probe/ProbeEvent.h"

namespace nf::icmpd
{

ProbeEvent::ProbeEvent(ProbeEventType type) : IcmpdEvent(IcmpdEventDomain::Probe), m_type(type)
{
}
ProbeEventType ProbeEvent::type() const
{
    return m_type;
}

void ProbeEvent::dispatch(IcmpdServiceManager& serviceManager)
{
    serviceManager.probeService().handleEvent(serviceManager, *this);
}

} // namespace nf::icmpd
