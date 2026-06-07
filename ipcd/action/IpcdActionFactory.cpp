#include "action/IpcdActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"

#include "util/Logger.h"

namespace pz::ipcd
{

std::unique_ptr<IpcdAction> IpcdActionFactory::create(IpcdActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case IpcdActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    default:
        LOG_WARN("IpcdActionFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace pz::ipcd
