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
    IcmpdEvent(IcmpdEventDomain::Bootstrap, std::move(message)), 
    m_type(type) 
{
}

BootstrapEventType BootstrapEvent::type() const
{
    return m_type;
}

void BootstrapEvent::dispatch(IcmpdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleEvent(serviceManager, *this);
}

} // namespace nf::icmpd
