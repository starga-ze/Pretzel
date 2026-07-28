#include "service/reload/ReloadEvent.h"

#include "service/ProbedServiceManager.h"
#include "service/reload/ReloadService.h"

namespace pz::probed
{

ReloadEvent::ReloadEvent(ReloadEventType type) : ProbedEvent(ProbedEventDomain::Reload), m_type(type)
{
}

void ReloadEvent::dispatch(ProbedServiceManager& serviceManager)
{
    serviceManager.reloadService().handleEvent(serviceManager, *this);
}

ReloadEventType ReloadEvent::type() const
{
    return m_type;
}

}
