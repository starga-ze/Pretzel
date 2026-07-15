#include "service/bootstrap/BootstrapEvent.h"
#include "service/IpcdServiceManager.h"

namespace pz::ipcd
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type) : IpcdEvent(IpcdEventDomain::Bootstrap), m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : IpcdEvent(IpcdEventDomain::Bootstrap), m_type(type), m_message(std::move(message))
{
}

void BootstrapEvent::dispatch(IpcdServiceManager& serviceManager)
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
