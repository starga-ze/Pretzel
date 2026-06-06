#include "action/EnginedActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"

#include "util/Logger.h"

namespace nf::engined
{

std::unique_ptr<EnginedAction> EnginedActionFactory::create(EnginedActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case EnginedActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    default:
        LOG_WARN("EnginedActionFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace nf::engined
