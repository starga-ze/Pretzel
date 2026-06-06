#include "action/MgmtdActionFactory.h"

#include "service/bootstrap/MgmtdBootstrapAction.h"

#include "util/Logger.h"

namespace nf::mgmtd
{

std::unique_ptr<MgmtdAction> MgmtdActionFactory::create(MgmtdActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case MgmtdActionDomain::Bootstrap:
        return std::make_unique<MgmtdBootstrapAction>(static_cast<MgmtdBootstrapActionType>(type));

    default:
        LOG_WARN("MgmtdActionFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace nf::mgmtd
