#include "action/CollectordActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"

#include "util/Logger.h"

namespace pz::collectord
{

std::unique_ptr<CollectordAction> CollectordActionFactory::create(CollectordActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case CollectordActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    default:
        LOG_WARN("unhandled domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

}
