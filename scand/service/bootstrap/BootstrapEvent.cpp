#include "service/bootstrap/BootstrapEvent.h"
#include "service/ScandServiceManager.h"

namespace pz::scand
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type) : ScandEvent(ScandEventDomain::Bootstrap), m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : ScandEvent(ScandEventDomain::Bootstrap), m_message(std::move(message)), m_type(type)
{
}

void BootstrapEvent::dispatch(ScandServiceManager& serviceManager)
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
