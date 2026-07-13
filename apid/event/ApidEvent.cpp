#include "event/ApidEvent.h"

namespace pz::apid
{

ApidEvent::ApidEvent(ApidEventDomain domain) : m_domain(domain)
{
}

ApidEventDomain ApidEvent::domain() const
{
    return m_domain;
}

} // namespace pz::apid
