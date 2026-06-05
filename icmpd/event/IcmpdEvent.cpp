#include "event/IcmpdEvent.h"

namespace nf::icmpd
{

IcmpdEvent::IcmpdEvent(IcmpdEventDomain domain) : m_domain(domain)
{
}

IcmpdEventDomain IcmpdEvent::domain() const
{
    return m_domain;
}

} // namespace nf::icmpd
