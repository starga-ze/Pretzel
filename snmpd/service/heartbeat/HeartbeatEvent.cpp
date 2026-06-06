#include "service/heartbeat/HeartbeatEvent.h"
#include "service/SnmpdServiceManager.h"

namespace nf::snmpd
{

HeartbeatEvent::HeartbeatEvent(HeartbeatEventType type) :
    SnmpdEvent(SnmpdEventDomain::Heartbeat),
    m_type(type)
{
}

HeartbeatEvent::HeartbeatEvent(HeartbeatEventType type, std::unique_ptr<nf::ipc::IpcMessage> message) :
    SnmpdEvent(SnmpdEventDomain::Heartbeat),
    m_message(std::move(message)),
    m_type(type)
{
}

void HeartbeatEvent::dispatch(SnmpdServiceManager& serviceManager)
{
    serviceManager.heartbeatService().handleEvent(serviceManager, *this);
}

HeartbeatEventType HeartbeatEvent::type() const
{
    return m_type;
}

const nf::ipc::IpcMessage* HeartbeatEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<nf::ipc::IpcMessage> HeartbeatEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace nf::snmpd
