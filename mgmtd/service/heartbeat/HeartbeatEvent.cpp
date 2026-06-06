#include "service/heartbeat/HeartbeatEvent.h"

#include "service/MgmtdServiceManager.h"

namespace nf::mgmtd
{

HeartbeatEvent::HeartbeatEvent(HeartbeatEventType type)
    : MgmtdEvent(MgmtdEventDomain::Heartbeat),
      m_type(type)
{
}

HeartbeatEvent::HeartbeatEvent(HeartbeatEventType type,
                               std::unique_ptr<nf::ipc::IpcMessage> message)
    : MgmtdEvent(MgmtdEventDomain::Heartbeat),
      m_type(type),
      m_message(std::move(message))
{
}

void HeartbeatEvent::dispatch(MgmtdServiceManager& serviceManager)
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

} // namespace nf::mgmtd
