#include "event/IcmpdEvent.h"

namespace pz::icmpd
{

IcmpdEvent::IcmpdEvent(IcmpdEventDomain domain) : m_domain(domain)
{
}

IcmpdEventDomain IcmpdEvent::domain() const
{
    return m_domain;
}

}
