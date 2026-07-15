#include "service/bootstrap/BootstrapEvent.h"

#include "service/TopologydServiceManager.h"

namespace pz::topologyd
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type) : TopologydEvent(TopologydEventDomain::Bootstrap), m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : TopologydEvent(TopologydEventDomain::Bootstrap), m_type(type), m_message(std::move(message))
{
}

void BootstrapEvent::dispatch(TopologydServiceManager& serviceManager)
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

}
