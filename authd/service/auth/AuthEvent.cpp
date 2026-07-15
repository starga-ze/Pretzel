#include "service/auth/AuthEvent.h"
#include "service/AuthdServiceManager.h"

namespace pz::authd
{

AuthEvent::AuthEvent(AuthEventType type) : AuthdEvent(AuthdEventDomain::Auth), m_type(type)
{
}

AuthEvent::AuthEvent(AuthEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : AuthdEvent(AuthdEventDomain::Auth), m_type(type), m_message(std::move(message))
{
}

void AuthEvent::dispatch(AuthdServiceManager& serviceManager)
{
    serviceManager.authService().handleEvent(serviceManager, *this);
}

AuthEventType AuthEvent::type() const
{
    return m_type;
}

const pz::ipc::IpcMessage* AuthEvent::message() const
{
    return m_message.get();
}

std::unique_ptr<pz::ipc::IpcMessage> AuthEvent::takeMessage()
{
    return std::move(m_message);
}

}
