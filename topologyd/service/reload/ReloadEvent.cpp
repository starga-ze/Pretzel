#include "service/reload/ReloadEvent.h"

#include "service/TopologydServiceManager.h"
#include "service/reload/ReloadService.h"

namespace pz::topologyd
{

ReloadEvent::ReloadEvent(ReloadEventType type) : TopologydEvent(TopologydEventDomain::Reload), m_type(type)
{
}

void ReloadEvent::dispatch(TopologydServiceManager& serviceManager)
{
    serviceManager.reloadService().handleEvent(serviceManager, *this);
}

ReloadEventType ReloadEvent::type() const
{
    return m_type;
}

}
