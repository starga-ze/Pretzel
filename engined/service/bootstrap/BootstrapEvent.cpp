#include "service/bootstrap/BootstrapEvent.h"
#include "service/EnginedServiceManager.h"

namespace pz::engined
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type)
    : EnginedEvent(EnginedEventDomain::Bootstrap),
      m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type,
                                             std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::Bootstrap),
      m_type(type),
      m_message(std::move(message))
{
}

void BootstrapEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleEvent(serviceManager, *this);
}

BootstrapEventType BootstrapEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* BootstrapEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<pz::ipc::IpcMessage> BootstrapEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace pz::engined
