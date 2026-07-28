#include "event/CollectordEvent.h"

namespace pz::collectord
{

CollectordEvent::CollectordEvent(CollectordEventDomain domain) : m_domain(domain)
{
}

CollectordEventDomain CollectordEvent::domain() const
{
    return m_domain;
}

}
