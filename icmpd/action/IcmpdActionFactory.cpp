#include "action/IcmpdActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"
#include "service/probe/ProbeAction.h"

#include "util/Logger.h"

namespace pz::icmpd
{

std::unique_ptr<IcmpdAction> IcmpdActionFactory::create(IcmpdActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case IcmpdActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    case IcmpdActionDomain::Probe:
        return std::make_unique<ProbeAction>(static_cast<ProbeActionType>(type));

    default:
        LOG_WARN("unhandled action domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

}
