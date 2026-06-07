#include "service/bootstrap/BootstrapAction.h"
#include "service/AuthdServiceManager.h"

namespace pz::authd
{

BootstrapAction::BootstrapAction(BootstrapActionType type) :
    AuthdAction(AuthdActionDomain::Bootstrap),
    m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

void BootstrapAction::dispatch(AuthdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

} // namespace pz::authd
