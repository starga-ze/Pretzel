#pragma once

#include "service/auth/AuthEvent.h"
#include "service/auth/AuthAction.h"
#include "service/auth/OktaClient.h"
#include "service/auth/SamlClient.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace pz::authd
{

class AuthdServiceManager;

// authd's auth authority: verifies local credentials (read from the local_users table,
// the same store mgmtd used to read directly) and drives the Okta OIDC transaction.
// It never issues sessions — it returns an authenticated identity to mgmtd, which owns
// the session cookie. Every handler echoes the request's src+seqNo on its response.
class AuthService
{
public:
    AuthService() = default;

    // Populates the Okta config from the authd config JSON (see integration doc for keys).
    // Safe to call with a missing "auth" section — OIDC just stays disabled.
    void configure(const nlohmann::json& authConfig);

    void handleEvent(AuthdServiceManager& serviceManager, const AuthEvent& event);
    void handleAction(AuthdServiceManager& serviceManager, const AuthAction& action);

    // Federated SSO method selected in config (service.auth.method): "oidc" | "saml".
    // Defaults to "oidc". SAML is wired once libxml2/xmlsec dev headers are installed.
    enum class Method { Oidc, Saml };
    Method method() const { return m_method; }

private:
    struct LocalResult
    {
        bool        success{false};
        std::string username;
        bool        mustChange{false};
    };

    // Verifies username/password against local_users. Fail-closed: an unprovisioned or
    // unreadable credential refuses every attempt (mirrors mgmtd's original semantics).
    LocalResult verifyLocal(const std::string& username,
                            const std::string& password) const;

    void respond(AuthdServiceManager& serviceManager,
                 AuthActionType type,
                 pz::ipc::IpcDaemon dst,
                 std::uint32_t seqNo,
                 const std::string& jsonPayload);

private:
    Method     m_method{Method::Oidc};
    OktaClient m_okta;
    SamlClient m_saml;
};

} // namespace pz::authd
