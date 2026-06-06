#include "action/IpcdActionFactory.h"

#include "service/bootstrap/IpcdBootstrapAction.h"

#include "util/Logger.h"

namespace nf::ipcd
{

std::unique_ptr<IpcdAction> IpcdActionFactory::create(IpcdActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case IpcdActionDomain::Bootstrap:
        return std::make_unique<IpcdBootstrapAction>(static_cast<IpcdBootstrapActionType>(type));

    default:
        LOG_WARN("IpcdActionFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace nf::ipcd
