#include "service/reload/ReloadEvent.h"

#include "service/AuthdServiceManager.h"
#include "service/reload/ReloadService.h"

namespace pz::authd
{

ReloadEvent::ReloadEvent(ReloadEventType type)
    : AuthdEvent(AuthdEventDomain::Reload),
      m_type(type)
{
}

void ReloadEvent::dispatch(AuthdServiceManager& serviceManager)
{
    serviceManager.reloadService().handleEvent(serviceManager, *this);
}

ReloadEventType ReloadEvent::type() const
{
    return m_type;
}

} // namespace pz::authd
