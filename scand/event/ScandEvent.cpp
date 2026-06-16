#include "event/ScandEvent.h"

namespace pz::scand
{

ScandEvent::ScandEvent(ScandEventDomain domain) : m_domain(domain)
{
}

ScandEventDomain ScandEvent::domain() const
{
    return m_domain;
}

} // namespace pz::scand
