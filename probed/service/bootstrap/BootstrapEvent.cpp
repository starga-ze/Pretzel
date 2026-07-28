#include "service/bootstrap/BootstrapEvent.h"
#include "service/ProbedServiceManager.h"

namespace pz::probed
{

BootstrapEvent::BootstrapEvent(BootstrapEventType type) : ProbedEvent(ProbedEventDomain::Bootstrap), m_type(type)
{
}

BootstrapEvent::BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : ProbedEvent(ProbedEventDomain::Bootstrap), m_message(std::move(message)), m_type(type)
{
}

void BootstrapEvent::dispatch(ProbedServiceManager& serviceManager)
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
