#include "service/bootstrap/BootstrapAction.h"
#include "service/CollectordServiceManager.h"

namespace pz::collectord
{

BootstrapAction::BootstrapAction(BootstrapActionType type) : CollectordAction(CollectordActionDomain::Bootstrap), m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

void BootstrapAction::dispatch(CollectordServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

}
