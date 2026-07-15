#include "service/bootstrap/BootstrapAction.h"
#include "service/EnginedServiceManager.h"

namespace pz::engined
{

BootstrapAction::BootstrapAction(BootstrapActionType type) : EnginedAction(EnginedActionDomain::Bootstrap), m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

void BootstrapAction::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

}
