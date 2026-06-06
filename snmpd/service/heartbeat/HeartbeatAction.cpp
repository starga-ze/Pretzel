#include "service/heartbeat/HeartbeatAction.h"
#include "service/SnmpdServiceManager.h"

namespace nf::snmpd
{

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, nf::ipc::IpcDaemon dst) :
    SnmpdAction(SnmpdActionDomain::Heartbeat),
    m_type(type),
    m_dst(dst)
{
}

HeartbeatActionType HeartbeatAction::type() const
{
    return m_type;
}

nf::ipc::IpcDaemon HeartbeatAction::dst() const
{
    return m_dst;
}

void HeartbeatAction::dispatch(SnmpdServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleAction(serviceManager, *this);
}

} // namespace nf::snmpd
