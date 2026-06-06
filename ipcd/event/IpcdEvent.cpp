#include "event/IpcdEvent.h"

namespace nf::ipcd
{

IpcdEvent::IpcdEvent(IpcdEventDomain domain) : m_domain(domain)
{
}

IpcdEventDomain IpcdEvent::domain() const
{
    return m_domain;
}

} // namespace nf::ipcd
