#include "action/TopologydActionFactory.h"

#include "service/bootstrap/BootstrapAction.h"
#include "service/heartbeat/HeartbeatAction.h"

#include "util/Logger.h"

namespace pz::topologyd
{

std::unique_ptr<TopologydAction> TopologydActionFactory::create(TopologydActionDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case TopologydActionDomain::Bootstrap:
        return std::make_unique<BootstrapAction>(static_cast<BootstrapActionType>(type));

    case TopologydActionDomain::Heartbeat:
        return std::make_unique<HeartbeatAction>(static_cast<HeartbeatActionType>(type), pz::ipc::IpcDaemon::Unknown);

    default:
        LOG_WARN("unhandled domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

}
