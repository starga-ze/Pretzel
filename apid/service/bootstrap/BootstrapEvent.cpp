#include "service/bootstrap/BootstrapEvent.h"
#include "service/ApidServiceManager.h"

namespace pz::apid
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type) : ApidEvent(ApidEventDomain::Bootstrap), m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : ApidEvent(ApidEventDomain::Bootstrap), m_type(type), m_message(std::move(message))
{
}

void BootstrapEvent::dispatch(ApidServiceManager& serviceManager)
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

}
