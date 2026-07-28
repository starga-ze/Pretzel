#include "service/reload/ReloadEvent.h"

#include "service/CollectordServiceManager.h"
#include "service/reload/ReloadService.h"

namespace pz::collectord
{

ReloadEvent::ReloadEvent(ReloadEventType type) : CollectordEvent(CollectordEventDomain::Reload), m_type(type)
{
}

void ReloadEvent::dispatch(CollectordServiceManager& serviceManager)
{
    serviceManager.reloadService().handleEvent(serviceManager, *this);
}

ReloadEventType ReloadEvent::type() const
{
    return m_type;
}

}
