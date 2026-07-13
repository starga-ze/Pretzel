#include "action/ApidActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"

#include "util/Logger.h"

namespace pz::apid
{

std::unique_ptr<ApidAction> ApidActionFactory::create(ApidActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case ApidActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    default:
        LOG_WARN("unhandled domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace pz::apid
