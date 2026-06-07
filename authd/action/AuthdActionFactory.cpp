#include "action/AuthdActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"

#include "util/Logger.h"

namespace pz::authd
{

std::unique_ptr<AuthdAction> AuthdActionFactory::create(AuthdActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case AuthdActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    default:
        LOG_WARN("AuthdActionFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace pz::authd
