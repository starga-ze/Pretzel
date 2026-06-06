#include "service/bootstrap/MgmtdBootstrapAction.h"
#include "service/MgmtdServiceManager.h"

namespace nf::mgmtd
{

MgmtdBootstrapAction::MgmtdBootstrapAction(MgmtdBootstrapActionType type)
    : MgmtdAction(MgmtdActionDomain::Bootstrap),
      m_type(type)
{
}

MgmtdBootstrapActionType MgmtdBootstrapAction::type() const
{
    return m_type;
}

void MgmtdBootstrapAction::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleAction(serviceManager, *this);
}

} // namespace nf::mgmtd
