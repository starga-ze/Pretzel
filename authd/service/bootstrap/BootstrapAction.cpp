#include "service/bootstrap/BootstrapAction.h"
#include "service/AuthdServiceManager.h"

namespace nf::authd
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

} // namespace nf::authd
