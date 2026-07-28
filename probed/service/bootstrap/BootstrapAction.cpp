#include "service/bootstrap/BootstrapAction.h"
#include "service/ProbedServiceManager.h"

namespace pz::probed
{

BootstrapAction::BootstrapAction(BootstrapActionType type) : ProbedAction(ProbedActionDomain::Bootstrap), m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

void BootstrapAction::dispatch(ProbedServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

}
