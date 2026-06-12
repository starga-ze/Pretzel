#include "service/reload/ReloadEvent.h"

#include "service/SnmpdServiceManager.h"
#include "service/reload/ReloadService.h"

namespace pz::snmpd
{

ReloadEvent::ReloadEvent(ReloadEventType type)
    : SnmpdEvent(SnmpdEventDomain::Reload),
      m_type(type)
{
}

void ReloadEvent::dispatch(SnmpdServiceManager& serviceManager)
{
    serviceManager.reloadService().handleEvent(serviceManager, *this);
}

ReloadEventType ReloadEvent::type() const
{
    return m_type;
}

} // namespace pz::snmpd
