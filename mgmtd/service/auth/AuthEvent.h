#pragma once

#include "event/MgmtdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::mgmtd
{

// An authentication answer from another daemon.
//
// AuthService had no event entry point before this: it is a set of synchronous helpers the web
// controllers call, which is right for login() and validateSession() — those answer inside the
// request that asked. SAML is the exception. The assertion is verified by authd, so the answer
// arrives later, over IPC, addressed to a ticket the browser is polling. That is an event, and it
// belongs to the auth domain rather than to whichever router happened to receive it.
enum class AuthEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveSamlAcsResponse = 1
};

class AuthEvent final : public MgmtdEvent
{
public:
    AuthEvent(AuthEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    AuthEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    AuthEventType m_type{AuthEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
