#include "event/TopologydEvent.h"

namespace pz::topologyd
{

TopologydEvent::TopologydEvent(TopologydEventDomain domain) : m_domain(domain)
{
}

TopologydEventDomain TopologydEvent::domain() const
{
    return m_domain;
}

}
