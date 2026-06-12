#include "service/reload/ReloadEvent.h"

#include "service/IcmpdServiceManager.h"
#include "service/reload/ReloadService.h"

namespace pz::icmpd
{

ReloadEvent::ReloadEvent(ReloadEventType type)
    : IcmpdEvent(IcmpdEventDomain::Reload),
      m_type(type)
{
}

void ReloadEvent::dispatch(IcmpdServiceManager& serviceManager)
{
    serviceManager.reloadService().handleEvent(serviceManager, *this);
}

ReloadEventType ReloadEvent::type() const
{
    return m_type;
}

} // namespace pz::icmpd
