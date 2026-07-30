#include "service/status/icmp/IcmpAction.h"

#include "service/ProbedServiceManager.h"
#include "service/status/icmp/IcmpService.h"

namespace pz::probed
{

IcmpAction::IcmpAction(IcmpActionType type) : ProbedAction(ProbedActionDomain::Icmp), m_type(type)
{
}

IcmpActionType IcmpAction::type() const
{
    return m_type;
}

void IcmpAction::dispatch(ProbedServiceManager& serviceManager)
{
    serviceManager.icmpService().handleAction(serviceManager, *this);
}

}
