#include "action/SnmpdActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"
#include "service/scan/ScanAction.h"

#include "util/Logger.h"

namespace pz::snmpd
{

std::unique_ptr<SnmpdAction> SnmpdActionFactory::create(SnmpdActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case SnmpdActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    case SnmpdActionDomain::Scan:
        return std::make_unique<ScanAction>(static_cast<ScanActionType>(type), "");

    default:
        LOG_WARN("unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace pz::snmpd
