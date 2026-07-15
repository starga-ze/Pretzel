#pragma once

#include "service/auth/AuthAction.h"
#include "service/auth/AuthEvent.h"
#include "service/auth/OktaClient.h"
#include "service/auth/SamlClient.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace pz::authd
{

class AuthdServiceManager;

class AuthService
{
public:
    AuthService() = default;

    void configure(const nlohmann::json& authConfig);

    void handleEvent(AuthdServiceManager& serviceManager, const AuthEvent& event);
    void handleAction(AuthdServiceManager& serviceManager, const AuthAction& action);

    enum class Method
    {
        Oidc,
        Saml
    };
    Method method() const
    {
        return m_method;
    }

private:
    struct LocalResult
    {
        bool success{false};
        std::string username;
        bool mustChange{false};
    };

    LocalResult verifyLocal(const std::string& username, const std::string& password) const;

    void respond(AuthdServiceManager& serviceManager, AuthActionType type, pz::ipc::IpcDaemon dst, std::uint32_t seqNo,
                 const std::string& jsonPayload);

private:
    Method m_method{Method::Oidc};
    OktaClient m_okta;
    SamlClient m_saml;
};

}
