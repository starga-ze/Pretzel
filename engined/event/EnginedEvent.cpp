#include "event/EnginedEvent.h"

namespace nf::engined
{

EnginedEvent::EnginedEvent(EnginedEventDomain domain) : m_domain(domain)
{
}

EnginedEventDomain EnginedEvent::domain() const
{
    return m_domain;
}

} // namespace nf::engined
