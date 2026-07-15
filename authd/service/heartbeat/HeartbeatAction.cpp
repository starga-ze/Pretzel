#include "service/heartbeat/HeartbeatAction.h"
#include "service/AuthdServiceManager.h"

namespace pz::authd
{

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst)
    : AuthdAction(AuthdActionDomain::Heartbeat), m_type(type), m_dst(dst)
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

void HeartbeatAction::dispatch(AuthdServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleAction(serviceManager, *this);
}

}
