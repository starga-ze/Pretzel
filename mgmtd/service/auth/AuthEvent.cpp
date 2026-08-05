#include "service/auth/AuthEvent.h"

#include "service/MgmtdServiceManager.h"
#include "service/auth/AuthService.h"

namespace pz::mgmtd
{

AuthEvent::AuthEvent(AuthEventType type, std::unique_ptr<pz::ipc::IpcMessage> message)
    : MgmtdEvent(MgmtdEventDomain::Auth), m_type(type), m_message(std::move(message))
{
}

void AuthEvent::dispatch(MgmtdServiceManager& serviceManager)
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

}
