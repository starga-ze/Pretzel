#include "service/advisory/AdvisoryEvent.h"

#include "service/EnginedServiceManager.h"

namespace pz::engined
{

AdvisoryEvent::AdvisoryEvent(AdvisoryEventType type)
    : EnginedEvent(EnginedEventDomain::Advisory), m_type(type)
{
}

AdvisoryEvent::AdvisoryEvent(AdvisoryEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::Advisory), m_type(type), m_message(std::move(message))
{
}

void AdvisoryEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.advisoryService().handleEvent(serviceManager, *this);
}

AdvisoryEventType AdvisoryEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* AdvisoryEvent::message() const
{
    return m_message.get();
}

}
