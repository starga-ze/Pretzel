#include "service/heartbeat/HeartbeatEvent.h"
#include "service/ApidServiceManager.h"

namespace pz::apid
{

HeartbeatEvent::HeartbeatEvent(HeartbeatEventType type) : ApidEvent(ApidEventDomain::Heartbeat), m_type(type)
{
}

HeartbeatEvent::HeartbeatEvent(HeartbeatEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : ApidEvent(ApidEventDomain::Heartbeat), m_type(type), m_message(std::move(message))
{
}

void HeartbeatEvent::dispatch(ApidServiceManager& serviceManager)
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
