#include "service/bootstrap/BootstrapEvent.h"
#include "service/IpcdServiceManager.h"

namespace nf::ipcd
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type)
    : IpcdEvent(IpcdEventDomain::Bootstrap),
      m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type,
                                       std::unique_ptr<nf::ipc::IpcMessage> message)
    : IpcdEvent(IpcdEventDomain::Bootstrap),
      m_type(type),
      m_message(std::move(message))
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

const nf::ipc::IpcMessage* BootstrapEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<nf::ipc::IpcMessage> BootstrapEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace nf::ipcd
