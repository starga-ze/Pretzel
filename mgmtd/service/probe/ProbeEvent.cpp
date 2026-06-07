#include "service/probe/ProbeEvent.h"
#include "service/MgmtdServiceManager.h"

namespace pz::mgmtd
{

ProbeEvent::ProbeEvent(ProbeEventType type)
    : MgmtdEvent(MgmtdEventDomain::Probe),
      m_type(type)
{
}

ProbeEvent::ProbeEvent(ProbeEventType type,
                                  std::unique_ptr<pz::ipc::IpcMessage> message)
    : MgmtdEvent(MgmtdEventDomain::Probe),
      m_type(type),
      m_message(std::move(message))
{
}

void ProbeEvent::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.probeService().handleEvent(serviceManager, *this);
}

ProbeEventType ProbeEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* ProbeEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<pz::ipc::IpcMessage> ProbeEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace pz::mgmtd
