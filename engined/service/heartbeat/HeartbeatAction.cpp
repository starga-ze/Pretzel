#include "service/heartbeat/HeartbeatAction.h"

#include "service/EnginedServiceManager.h"

namespace pz::engined
{

HeartbeatAction::HeartbeatAction(HeartbeatActionType type) : EnginedAction(EnginedActionDomain::Heartbeat), m_type(type)
{
}

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, pz::ipc::IpcDaemon dst)
    : EnginedAction(EnginedActionDomain::Heartbeat), m_type(type), m_dst(dst)
{
}

HeartbeatAction::HeartbeatAction(HeartbeatActionType type, std::string resultJson)
    : EnginedAction(EnginedActionDomain::Heartbeat), m_type(type), m_resultJson(std::move(resultJson))
{
}

void HeartbeatAction::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleAction(serviceManager, *this);
}

HeartbeatActionType HeartbeatAction::type() const
{
    return m_type;
}

pz::ipc::IpcDaemon HeartbeatAction::dst() const
{
    return m_dst;
}

const std::string& HeartbeatAction::resultJson() const
{
    return m_resultJson;
}

}
