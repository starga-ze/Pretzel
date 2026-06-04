#include "service/IcmpdServiceManager.h"
#include "service/probe/ProbeAction.h"

namespace nf::icmpd
{

ProbeAction::ProbeAction(ProbeActionType type) : IcmpdAction(IcmpdActionDomain::Probe), m_type(type)
{
}

ProbeActionType ProbeAction::type() const
{
    return m_type;
}

void ProbeAction::dispatch(IcmpdServiceManager& serviceManager)
{
    serviceManager.probeService().handleAction(serviceManager, *this);
}

} // namespace nf::icmpd
