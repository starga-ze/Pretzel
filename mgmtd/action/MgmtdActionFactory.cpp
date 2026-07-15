#include "action/MgmtdActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"

#include "util/Logger.h"

namespace pz::mgmtd
{

std::unique_ptr<MgmtdAction> MgmtdActionFactory::create(MgmtdActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case MgmtdActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    default:
        LOG_WARN("unhandled domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

}
