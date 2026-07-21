#include "service/apikey/ApiKeyEvent.h"

#include "service/EnginedServiceManager.h"

namespace pz::engined
{

ApiKeyEvent::ApiKeyEvent(ApiKeyEventType type) : EnginedEvent(EnginedEventDomain::ApiKey), m_type(type)
{
}

ApiKeyEvent::ApiKeyEvent(ApiKeyEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::ApiKey), m_type(type), m_message(std::move(message))
{
}

void ApiKeyEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.apiKeyService().handleEvent(serviceManager, *this);
}

ApiKeyEventType ApiKeyEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* ApiKeyEvent::message() const
{
    return m_message.get();
}

}
