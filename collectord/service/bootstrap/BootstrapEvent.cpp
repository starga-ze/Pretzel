#include "service/bootstrap/BootstrapEvent.h"
#include "service/CollectordServiceManager.h"

namespace pz::collectord
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type) : CollectordEvent(CollectordEventDomain::Bootstrap), m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : CollectordEvent(CollectordEventDomain::Bootstrap), m_message(std::move(message)), m_type(type)
{
}

void BootstrapEvent::dispatch(CollectordServiceManager& serviceManager)
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
