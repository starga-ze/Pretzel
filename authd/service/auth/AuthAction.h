#pragma once

#include "action/AuthdAction.h"
#include "ipc/IpcProtocol.h"

#include <cstdint>
#include <string>

namespace pz::authd
{

enum class AuthActionType : std::uint32_t
{
    Unknown                  = 0,
    SendLoginResponse        = 1,
    SendOidcStartResponse    = 2,
    SendOidcCallbackResponse = 3,
    SendSamlStartResponse    = 4,
    SendSamlAcsResponse      = 5
};

// A response action: it targets the original requester (dst) and echoes the request
// seqNo so the requester correlates the reply. `payload` is the response JSON body.
class AuthAction final : public AuthdAction
{
public:
    AuthAction(AuthActionType type,
               pz::ipc::IpcDaemon dst,
               std::uint32_t seqNo,
               std::string payload);

    AuthActionType type() const;
    pz::ipc::IpcDaemon dst() const;
    std::uint32_t seqNo() const;
    const std::string& payload() const;

    void dispatch(AuthdServiceManager& serviceManager) override;

private:
    AuthActionType m_type{AuthActionType::Unknown};
    pz::ipc::IpcDaemon m_dst{pz::ipc::IpcDaemon::Unknown};
    std::uint32_t m_seqNo{0};
    std::string m_payload;
};

} // namespace pz::authd
