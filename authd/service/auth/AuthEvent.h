#pragma once

#include "event/AuthdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::authd
{

enum class AuthEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveLoginRequest = 1,
    ReceiveOidcStartRequest = 2,
    ReceiveOidcCallbackRequest = 3,
    ReceiveSamlStartRequest = 4,
    ReceiveSamlAcsRequest = 5
};

class AuthEvent final : public AuthdEvent
{
public:
    explicit AuthEvent(AuthEventType type);

    AuthEvent(AuthEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(AuthdServiceManager& serviceManager) override;

    AuthEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    AuthEventType m_type{AuthEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
