#include "service/bootstrap/BootstrapEvent.h"
#include "service/SnmpdServiceManager.h"

namespace nf::snmpd
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type) :
    SnmpdEvent(SnmpdEventDomain::Bootstrap),
    m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type, std::unique_ptr<nf::ipc::IpcMessage> message) :
    SnmpdEvent(SnmpdEventDomain::Bootstrap),
    m_message(std::move(message)),
    m_type(type)
{
}

void BootstrapEvent::dispatch(SnmpdServiceManager& serviceManager)
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

} // namespace nf::snmpd
