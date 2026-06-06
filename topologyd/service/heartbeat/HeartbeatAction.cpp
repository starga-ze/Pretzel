#include "service/heartbeat/HeartbeatAction.h"

#include "service/TopologydServiceManager.h"

namespace nf::topologyd
{

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, nf::ipc::IpcDaemon dst)
    : TopologydAction(TopologydActionDomain::Heartbeat),
      m_type(type),
      m_dst(dst)
{
}

void HeartbeatAction::dispatch(TopologydServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleAction(serviceManager, *this);
}

HeartbeatActionType HeartbeatAction::type() const
{
    return m_type;
}

nf::ipc::IpcDaemon HeartbeatAction::dst() const
{
    return m_dst;
}

} // namespace nf::topologyd
