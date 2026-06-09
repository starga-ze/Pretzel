#include "action/EnginedActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"
#include "service/heartbeat/HeartbeatAction.h"

#include "util/Logger.h"

namespace pz::engined
{

std::unique_ptr<EnginedAction> EnginedActionFactory::create(EnginedActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case EnginedActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    case EnginedActionDomain::Heartbeat:
        return std::make_unique<HeartbeatAction>(
            static_cast<HeartbeatActionType>(type),
            pz::ipc::IpcDaemon::Unknown);

    default:
        LOG_WARN("unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace pz::engined
