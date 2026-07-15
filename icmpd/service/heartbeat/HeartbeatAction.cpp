#include "service/heartbeat/HeartbeatAction.h"
#include "service/IcmpdServiceManager.h"

namespace pz::icmpd
{

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst)
    : IcmpdAction(IcmpdActionDomain::Heartbeat), m_type(type), m_dst(dst)
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

void HeartbeatAction::dispatch(IcmpdServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleAction(serviceManager, *this);
}

}
