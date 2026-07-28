#include "service/heartbeat/HeartbeatAction.h"
#include "service/ProbedServiceManager.h"

namespace pz::probed
{

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst)
    : ProbedAction(ProbedActionDomain::Heartbeat), m_type(type), m_dst(dst)
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

void HeartbeatAction::dispatch(ProbedServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleAction(serviceManager, *this);
}

}
