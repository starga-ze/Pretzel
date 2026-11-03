#include "service/advisory/AdvisoryEvent.h"

#include "service/CollectordServiceManager.h"

namespace pz::collectord
{

AdvisoryEvent::AdvisoryEvent(AdvisoryEventType type)
    : CollectordEvent(CollectordEventDomain::Advisory), m_type(type)
{
}

AdvisoryEvent::AdvisoryEvent(AdvisoryEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : CollectordEvent(CollectordEventDomain::Advisory), m_type(type), m_message(std::move(message))
{
}

void AdvisoryEvent::dispatch(CollectordServiceManager& serviceManager)
{
    serviceManager.advisoryService().handleEvent(serviceManager, *this);
}

AdvisoryEventType AdvisoryEvent::type() const
{
    return m_type;
}

}
