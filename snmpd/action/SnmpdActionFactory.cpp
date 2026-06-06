#include "action/SnmpdActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"

#include "util/Logger.h"

namespace nf::snmpd
{

std::unique_ptr<SnmpdAction> SnmpdActionFactory::create(SnmpdActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case SnmpdActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    default:
        LOG_WARN("SnmpdActionFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace nf::snmpd
