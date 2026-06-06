#include "service/bootstrap/IpcdBootstrapEvent.h"
#include "service/IpcdServiceManager.h"

namespace nf::ipcd
{

IpcdBootstrapEvent::IpcdBootstrapEvent(IpcdBootstrapEventType type)
    : IpcdEvent(IpcdEventDomain::Bootstrap),
      m_type(type)
{
}

IpcdBootstrapEvent::IpcdBootstrapEvent(IpcdBootstrapEventType type,
                                       std::unique_ptr<nf::ipc::IpcMessage> message)
    : IpcdEvent(IpcdEventDomain::Bootstrap),
      m_type(type),
      m_message(std::move(message))
{
}

void IpcdBootstrapEvent::dispatch(IpcdServiceManager& serviceManager)
{
    serviceManager.bootstrapService().handleEvent(serviceManager, *this);
}

IpcdBootstrapEventType IpcdBootstrapEvent::type() const
{
    return m_type;
}

const nf::ipc::IpcMessage* IpcdBootstrapEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<nf::ipc::IpcMessage> IpcdBootstrapEvent::takeMessage()
{
    return std::move(m_message);
}

} // namespace nf::ipcd
