#include "event/AuthdEvent.h"

namespace pz::authd
{

AuthdEvent::AuthdEvent(AuthdEventDomain domain) : m_domain(domain)
{
}

AuthdEventDomain AuthdEvent::domain() const
{
    return m_domain;
}

} // namespace pz::authd
