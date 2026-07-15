#include "service/heartbeat/HeartbeatAction.h"
#include "service/MgmtdServiceManager.h"

namespace pz::mgmtd
{

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst)
    : MgmtdAction(MgmtdActionDomain::Heartbeat), m_type(type), m_dst(dst)
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

void HeartbeatAction::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleAction(serviceManager, *this);
}

}
