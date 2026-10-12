#include "service/bootstrap/BootstrapAction.h"
#include "service/EnginedServiceManager.h"

namespace pz::engined
{

BootstrapAction::BootstrapAction(BootstrapActionType type) : EnginedAction(EnginedActionDomain::Bootstrap), m_type(type)
{
}

void BootstrapAction::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

}
