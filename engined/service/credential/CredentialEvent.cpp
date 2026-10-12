#include "service/credential/CredentialEvent.h"

#include "service/EnginedServiceManager.h"

namespace pz::engined
{

CredentialEvent::CredentialEvent(CredentialEventType type) : EnginedEvent(EnginedEventDomain::Credential), m_type(type)
{
}

CredentialEvent::CredentialEvent(CredentialEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : EnginedEvent(EnginedEventDomain::Credential), m_type(type), m_message(std::move(message))
{
}

void CredentialEvent::dispatch(EnginedServiceManager& serviceManager)
{
    serviceManager.credentialService().handleEvent(serviceManager, *this);
}

CredentialEventType CredentialEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* CredentialEvent::message() const
{
    return m_message.get();
}

}
