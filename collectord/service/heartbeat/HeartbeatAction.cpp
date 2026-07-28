#include "service/heartbeat/HeartbeatAction.h"
#include "service/CollectordServiceManager.h"

namespace pz::collectord
{

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst)
    : CollectordAction(CollectordActionDomain::Heartbeat), m_type(type), m_dst(dst)
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

void HeartbeatAction::dispatch(CollectordServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleAction(serviceManager, *this);
}

}
