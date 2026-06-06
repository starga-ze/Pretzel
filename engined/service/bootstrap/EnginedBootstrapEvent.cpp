#include "service/bootstrap/EnginedBootstrapEvent.h"
#include "service/EnginedServiceManager.h"

namespace nf::engined
{

EnginedBootstrapEvent::EnginedBootstrapEvent(EnginedBootstrapEventType type)
    : EnginedEvent(EnginedEventDomain::Bootstrap),
      m_type(type)
{
}

EnginedBootstrapEvent::EnginedBootstrapEvent(EnginedBootstrapEventType type,
                                             std::unique_ptr<nf::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::Bootstrap),
      m_type(type),
      m_message(std::move(message))
{
}

void EnginedBootstrapEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleEvent(serviceManager, *this);
}

EnginedBootstrapEventType EnginedBootstrapEvent::type() const
{
    return m_type;
}

const nf::ipc::IpcMessage* EnginedBootstrapEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<nf::ipc::IpcMessage> EnginedBootstrapEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace nf::engined
