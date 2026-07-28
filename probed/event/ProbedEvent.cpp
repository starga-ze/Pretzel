#include "event/ProbedEvent.h"

namespace pz::probed
{

ProbedEvent::ProbedEvent(ProbedEventDomain domain) : m_domain(domain)
{
}

ProbedEventDomain ProbedEvent::domain() const
{
    return m_domain;
}

}
