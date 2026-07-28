#include "action/ProbedActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"
#include "service/icmp/IcmpAction.h"

#include "util/Logger.h"

namespace pz::probed
{

std::unique_ptr<ProbedAction> ProbedActionFactory::create(ProbedActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case ProbedActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    case ProbedActionDomain::Icmp:
        return std::make_unique<IcmpAction>(static_cast<IcmpActionType>(type));

    default:
        LOG_WARN("unhandled action domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

}
