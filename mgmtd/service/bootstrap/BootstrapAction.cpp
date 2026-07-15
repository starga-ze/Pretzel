#include "service/bootstrap/BootstrapAction.h"
#include "service/MgmtdServiceManager.h"

namespace pz::mgmtd
{

BootstrapAction::BootstrapAction(BootstrapActionType type) : MgmtdAction(MgmtdActionDomain::Bootstrap), m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

void BootstrapAction::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

}
