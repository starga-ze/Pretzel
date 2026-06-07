#include "service/bootstrap/BootstrapAction.h"

#include "service/TopologydServiceManager.h"

namespace pz::topologyd
{

BootstrapAction::BootstrapAction(BootstrapActionType type)
    : TopologydAction(TopologydActionDomain::Bootstrap),
      m_type(type)
{
}

void BootstrapAction::dispatch(TopologydServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

} // namespace pz::topologyd
