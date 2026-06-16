#include "service/heartbeat/HeartbeatAction.h"
#include "service/ScandServiceManager.h"

namespace pz::scand
{

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst) :
    ScandAction(ScandActionDomain::Heartbeat),
    m_type(type),
    m_dst(dst)
{
}

HeartbeatActionType HeartbeatAction::type() const
{
    return m_type;
}

pz::ipc::IpcDaemon HeartbeatAction::dst() const
{
    return m_dst;
}

void HeartbeatAction::dispatch(ScandServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleAction(serviceManager, *this);
}

} // namespace pz::scand
