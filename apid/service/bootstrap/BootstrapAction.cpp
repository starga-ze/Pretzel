#include "service/bootstrap/BootstrapAction.h"
#include "service/ApidServiceManager.h"

namespace pz::apid
{

BootstrapAction::BootstrapAction(BootstrapActionType type)
    : ApidAction(ApidActionDomain::Bootstrap),
      m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

void BootstrapAction::dispatch(ApidServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

} // namespace pz::apid
