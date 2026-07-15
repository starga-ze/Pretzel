#include "service/bootstrap/BootstrapEvent.h"
#include "service/IcmpdServiceManager.h"

namespace pz::icmpd
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type) : IcmpdEvent(IcmpdEventDomain::Bootstrap), m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : IcmpdEvent(IcmpdEventDomain::Bootstrap), m_message(std::move(message)), m_type(type)
{
}

void BootstrapEvent::dispatch(IcmpdServiceManager& serviceManager)
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
