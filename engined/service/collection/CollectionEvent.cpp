#include "service/collection/CollectionEvent.h"

#include "service/EnginedServiceManager.h"

namespace pz::engined
{

CollectionEvent::CollectionEvent(CollectionEventType type)
    : EnginedEvent(EnginedEventDomain::Collection), m_type(type)
{
}

CollectionEvent::CollectionEvent(CollectionEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::Collection), m_type(type), m_message(std::move(message))
{
}

void CollectionEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.collectionService().handleEvent(serviceManager, *this);
}

CollectionEventType CollectionEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* CollectionEvent::message() const
{
    return m_message.get();
}

}
