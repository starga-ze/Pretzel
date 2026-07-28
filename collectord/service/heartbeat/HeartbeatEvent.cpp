#include "service/heartbeat/HeartbeatEvent.h"
#include "service/CollectordServiceManager.h"

namespace pz::collectord
{

HeartbeatEvent::HeartbeatEvent(HeartbeatEventType type) : CollectordEvent(CollectordEventDomain::Heartbeat), m_type(type)
{
}

HeartbeatEvent::HeartbeatEvent(HeartbeatEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : CollectordEvent(CollectordEventDomain::Heartbeat), m_message(std::move(message)), m_type(type)
{
}

void HeartbeatEvent::dispatch(CollectordServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleEvent(serviceManager, *this);
}

HeartbeatEventType HeartbeatEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* HeartbeatEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<pz::ipc::IpcMessage> HeartbeatEvent::takeMessage()
{
    return std::move(m_message);
}

}
