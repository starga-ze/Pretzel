#include "service/probe/ProbeEvent.h"

#include "service/EnginedServiceManager.h"

namespace pz::engined
{

ProbeEvent::ProbeEvent(ProbeEventType type)
    : EnginedEvent(EnginedEventDomain::Probe),
      m_type(type)
{
}

ProbeEvent::ProbeEvent(ProbeEventType type,
                       std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::Probe),
      m_type(type),
      m_message(std::move(message))
{
}

void ProbeEvent::dispatch(EnginedServiceManager& serviceManager)
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

} // namespace pz::engined
