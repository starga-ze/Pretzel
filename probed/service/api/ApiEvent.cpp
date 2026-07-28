#include "service/api/ApiEvent.h"
#include "service/ProbedServiceManager.h"

namespace pz::probed
{

ApiEvent::ApiEvent(ApiEventType type) : ProbedEvent(ProbedEventDomain::Api), m_type(type)
{
}

ApiEvent::ApiEvent(ApiEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : ProbedEvent(ProbedEventDomain::Api), m_type(type), m_message(std::move(message))
{
}

void ApiEvent::dispatch(ProbedServiceManager& serviceManager)
{
    serviceManager.apiService().handleEvent(serviceManager, *this);
}

ApiEventType ApiEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* ApiEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<pz::ipc::IpcMessage> ApiEvent::takeMessage()
{
    return std::move(m_message);
}

}
