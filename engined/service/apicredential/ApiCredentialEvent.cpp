#include "service/apicredential/ApiCredentialEvent.h"

#include "service/EnginedServiceManager.h"

namespace pz::engined
{

ApiCredentialEvent::ApiCredentialEvent(ApiCredentialEventType type) : EnginedEvent(EnginedEventDomain::ApiCredential), m_type(type)
{
}

ApiCredentialEvent::ApiCredentialEvent(ApiCredentialEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::ApiCredential), m_type(type), m_message(std::move(message))
{
}

void ApiCredentialEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.apiCredentialService().handleEvent(serviceManager, *this);
}

ApiCredentialEventType ApiCredentialEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* ApiCredentialEvent::message() const
{
    return m_message.get();
}

}
