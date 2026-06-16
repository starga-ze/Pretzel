#include "service/bootstrap/BootstrapAction.h"
#include "service/ScandServiceManager.h"

namespace pz::scand
{

BootstrapAction::BootstrapAction(BootstrapActionType type) :
    ScandAction(ScandActionDomain::Bootstrap),
    m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

void BootstrapAction::dispatch(ScandServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

} // namespace pz::scand
