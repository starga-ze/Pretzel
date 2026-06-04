#include "service/bootstrap/BootstrapEvent.h"
#include "service/IcmpdServiceManager.h"

namespace nf::icmpd
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type) : 
    IcmpdEvent(IcmpdEventDomain::Bootstrap), 
    m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type, std::unique_ptr<nf::ipc::IpcMessage> message) : 
    IcmpdEvent(IcmpdEventDomain::Bootstrap), 
    m_message(std::move(message)),
    m_type(type) 
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

const nf::ipc::IpcMessage* BootstrapEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<nf::ipc::IpcMessage> BootstrapEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace nf::icmpd
