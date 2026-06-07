#include "service/bootstrap/BootstrapAction.h"
#include "service/SnmpdServiceManager.h"

namespace pz::snmpd
{

BootstrapAction::BootstrapAction(BootstrapActionType type) :
    SnmpdAction(SnmpdActionDomain::Bootstrap),
    m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

void BootstrapAction::dispatch(SnmpdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

} // namespace pz::snmpd
