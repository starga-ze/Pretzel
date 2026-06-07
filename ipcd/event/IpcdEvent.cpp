#include "event/IpcdEvent.h"

namespace pz::ipcd
{

IpcdEvent::IpcdEvent(IpcdEventDomain domain) : m_domain(domain)
{
}

IpcdEventDomain IpcdEvent::domain() const
{
    return m_domain;
}

} // namespace pz::ipcd
