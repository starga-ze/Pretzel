#include "service/bootstrap/EnginedBootstrapAction.h"
#include "service/EnginedServiceManager.h"

namespace nf::engined
{

EnginedBootstrapAction::EnginedBootstrapAction(EnginedBootstrapActionType type)
    : EnginedAction(EnginedActionDomain::Bootstrap),
      m_type(type)
{
}

EnginedBootstrapActionType EnginedBootstrapAction::type() const
{
    return m_type;
}

void EnginedBootstrapAction::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

} // namespace nf::engined
