#include "action/ScandActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"
#include "service/scan/ScanAction.h"

#include "util/Logger.h"

namespace pz::scand
{

std::unique_ptr<ScandAction> ScandActionFactory::create(ScandActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case ScandActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    case ScandActionDomain::Scan:
        return std::make_unique<ScanAction>(static_cast<ScanActionType>(type), "");

    default:
        LOG_WARN("unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace pz::scand
