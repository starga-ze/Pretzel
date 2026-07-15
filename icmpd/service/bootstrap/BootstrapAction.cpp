#include "service/bootstrap/BootstrapAction.h"
#include "service/IcmpdServiceManager.h"

namespace pz::icmpd
{

BootstrapAction::BootstrapAction(BootstrapActionType type) : IcmpdAction(IcmpdActionDomain::Bootstrap), m_type(type)
{
}

BootstrapActionType BootstrapAction::type() const
{
    return m_type;
}

void BootstrapAction::dispatch(IcmpdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

}
