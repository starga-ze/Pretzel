#include "event/SnmpdEvent.h"

namespace pz::snmpd
{

SnmpdEvent::SnmpdEvent(SnmpdEventDomain domain) : m_domain(domain)
{
}

SnmpdEventDomain SnmpdEvent::domain() const
{
    return m_domain;
}

} // namespace pz::snmpd
