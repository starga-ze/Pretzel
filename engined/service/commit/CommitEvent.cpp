#include "service/commit/CommitEvent.h"
#include "service/EnginedServiceManager.h"

namespace pz::engined
{

CommitEvent::CommitEvent(CommitEventType type) : EnginedEvent(EnginedEventDomain::Commit), m_type(type)
{
}

CommitEvent::CommitEvent(CommitEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::Commit), m_type(type), m_message(std::move(message))
{
}

void CommitEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.commitService().handleEvent(serviceManager, *this);
}

CommitEventType CommitEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* CommitEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<pz::ipc::IpcMessage> CommitEvent::takeMessage()
{
    return std::move(m_message);
}

}
