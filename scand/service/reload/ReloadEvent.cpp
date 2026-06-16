#include "service/reload/ReloadEvent.h"

#include "service/ScandServiceManager.h"
#include "service/reload/ReloadService.h"

namespace pz::scand
{

ReloadEvent::ReloadEvent(ReloadEventType type)
    : ScandEvent(ScandEventDomain::Reload),
      m_type(type)
{
}

void ReloadEvent::dispatch(ScandServiceManager& serviceManager)
{
    serviceManager.reloadService().handleEvent(serviceManager, *this);
}

ReloadEventType ReloadEvent::type() const
{
    return m_type;
}

} // namespace pz::scand
