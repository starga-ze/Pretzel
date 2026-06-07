#include "event/MgmtdEvent.h"

namespace pz::mgmtd
{

MgmtdEvent::MgmtdEvent(MgmtdEventDomain domain) : m_domain(domain)
{
}

MgmtdEventDomain MgmtdEvent::domain() const
{
    return m_domain;
}

} // namespace pz::mgmtd
