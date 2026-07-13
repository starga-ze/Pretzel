#pragma once

#include "event/AuthdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::authd
{

enum class AuthEventType : std::uint32_t
{
    Unknown                    = 0,
    ReceiveLoginRequest        = 1,  // mgmtd -> authd, verify local credential
    ReceiveOidcStartRequest    = 2,  // mgmtd -> authd, build Okta authorize URL
    ReceiveOidcCallbackRequest = 3,  // mgmtd -> authd, exchange code + verify id_token
    ReceiveSamlStartRequest    = 4,  // mgmtd -> authd, build SAML AuthnRequest redirect
    ReceiveSamlAcsRequest      = 5   // mgmtd -> authd, verify SAMLResponse from the ACS
};

// Carries the originating IPC request so the service can echo src + seqNo back on
// the response (request/response correlation is by seqNo, same as every other daemon).
class AuthEvent final : public AuthdEvent
{
public:
    explicit AuthEvent(AuthEventType type);

    AuthEvent(AuthEventType type,
              std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(AuthdServiceManager& serviceManager) override;

    AuthEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    AuthEventType m_type{AuthEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

} // namespace pz::authd
