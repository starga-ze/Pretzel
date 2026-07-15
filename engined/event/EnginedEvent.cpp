#include "event/EnginedEvent.h"

namespace pz::engined
{

EnginedEvent::EnginedEvent(EnginedEventDomain domain) : m_domain(domain)
{
}

EnginedEventDomain EnginedEvent::domain() const
{
    return m_domain;
}

}
