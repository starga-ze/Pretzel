#include "service/auth/AuthService.h"

#include "service/AuthdServiceManager.h"

#include "db/Database.h"
#include "util/Logger.h"
#include "util/PasswordHash.h"

#include <nlohmann/json.hpp>

namespace pz::authd
{

namespace
{
std::string payloadToString(const pz::ipc::IpcMessage& msg)
{
    const auto& p = msg.getPayload();
    return std::string(p.begin(), p.end());
}
}

void AuthService::configure(const nlohmann::json& authConfig)
{
    m_method = Method::Oidc;
    try
    {
        const std::string m = authConfig.value("method", std::string("oidc"));
        m_method = (m == "saml") ? Method::Saml : Method::Oidc;
        LOG_INFO("auth: federated method = {}", (m_method == Method::Saml) ? "saml" : "oidc");
    }
    catch (const std::exception&)
    {
    }

    OktaClient::Config cfg;
    try
    {
        if (authConfig.contains("oidc"))
        {
            const auto& o = authConfig["oidc"];
            cfg.enabled = o.value("enabled", false);
            cfg.issuer = o.value("issuer", "");
            cfg.clientId = o.value("client_id", "");
            cfg.clientSecret = o.value("client_secret", "");
            cfg.redirectUri = o.value("redirect_uri", "");
            cfg.scopes = o.value("scopes", std::string("openid email profile"));
            cfg.verifyTls = o.value("verify_tls", true);
            cfg.timeoutMs = o.value("timeout_ms", 5000);
            cfg.txnTtlSec = o.value("txn_ttl_sec", std::uint64_t{300});
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("auth: bad oidc config ({}) — oidc disabled", e.what());
        cfg = {};
    }
    m_okta.configure(cfg);

    SamlClient::Config scfg;
    try
    {
        if (authConfig.contains("saml"))
        {
            const auto& s = authConfig["saml"];
            scfg.enabled = s.value("enabled", false);
            scfg.idpSsoUrl = s.value("idp_sso_url", "");
            scfg.idpEntityId = s.value("idp_entity_id", "");
            scfg.idpCertPem = s.value("idp_cert_pem", "");
            scfg.spEntityId = s.value("sp_entity_id", "");
            scfg.acsUrl = s.value("acs_url", "");
            scfg.adminGroup = s.value("admin_group", "");
            scfg.groupsAttr = s.value("groups_attr", std::string("groups"));
            scfg.emailAttr = s.value("email_attr", std::string("email"));
            scfg.clockSkewSec = s.value("clock_skew_sec", std::uint64_t{120});
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("auth: bad saml config ({}) — saml disabled", e.what());
        scfg = {};
    }
    m_saml.configure(scfg);
}

AuthService::LocalResult AuthService::verifyLocal(const std::string& username, const std::string& password) const
{
    LocalResult r;

    const auto rows =
        pz::db::Database::instance().queryRows("SELECT username, password_hash, salt, must_change FROM local_users "
                                               "WHERE username = $1 LIMIT 1",
                                               {username});

    if (rows.empty() || rows.front().size() < 4)
    {
        LOG_WARN("auth: no readable local_users row for '{}' — refusing", username);
        return r;
    }

    const std::string& hash = rows.front()[1];
    const std::string& salt = rows.front()[2];
    const bool mustChange = (rows.front()[3] == "t");

    if (hash.empty() || !pz::util::verifyPassword(password, salt, hash))
    {
        return r;
    }

    r.success = true;
    r.username = rows.front()[0];
    r.mustChange = mustChange;
    return r;
}

void AuthService::handleEvent(AuthdServiceManager& serviceManager, const AuthEvent& event)
{
    const auto* msg = event.message();
    if (!msg)
    {
        LOG_WARN("auth: event without message (type={})", static_cast<std::uint32_t>(event.type()));
        return;
    }

    const pz::ipc::IpcDaemon dst = msg->getSrc();
    const std::uint32_t seqNo = msg->getSeqNo();

    switch (event.type())
    {
    case AuthEventType::ReceiveLoginRequest:
    {
        std::string username, password;
        try
        {
            const auto req = nlohmann::json::parse(payloadToString(*msg));
            username = req.value("username", "");
            password = req.value("password", "");
        }
        catch (const std::exception& e)
        {
            LOG_WARN("auth: bad login request ({})", e.what());
        }

        const auto res = verifyLocal(username, password);

        nlohmann::json out;
        out["success"] = res.success;
        out["username"] = res.username;
        out["must_change"] = res.mustChange;
        if (!res.success)
            out["error"] = "invalid credentials";

        respond(serviceManager, AuthActionType::SendLoginResponse, dst, seqNo, out.dump());
        break;
    }

    case AuthEventType::ReceiveOidcStartRequest:
    {
        if (m_method != Method::Oidc)
        {
            nlohmann::json out{{"success", false}, {"error", "oidc not selected"}};
            respond(serviceManager, AuthActionType::SendOidcStartResponse, dst, seqNo, out.dump());
            break;
        }
        const auto start = m_okta.buildAuthorizeUrl();

        nlohmann::json out;
        out["success"] = start.success;
        if (start.success)
        {
            out["authorize_url"] = start.authorizeUrl;
            out["state"] = start.state;
        }
        else
        {
            out["error"] = start.error;
        }

        respond(serviceManager, AuthActionType::SendOidcStartResponse, dst, seqNo, out.dump());
        break;
    }

    case AuthEventType::ReceiveOidcCallbackRequest:
    {
        if (m_method != Method::Oidc)
        {
            nlohmann::json out{{"success", false}, {"error", "oidc not selected"}};
            respond(serviceManager, AuthActionType::SendOidcCallbackResponse, dst, seqNo, out.dump());
            break;
        }
        std::string code, state;
        try
        {
            const auto req = nlohmann::json::parse(payloadToString(*msg));
            code = req.value("code", "");
            state = req.value("state", "");
        }
        catch (const std::exception& e)
        {
            LOG_WARN("auth: bad oidc callback ({})", e.what());
        }

        const auto res = m_okta.exchangeAndVerify(code, state);

        nlohmann::json out;
        out["success"] = res.success;
        out["username"] = res.username;
        if (!res.success)
            out["error"] = res.error;

        respond(serviceManager, AuthActionType::SendOidcCallbackResponse, dst, seqNo, out.dump());
        break;
    }

    case AuthEventType::ReceiveSamlStartRequest:
    {
        nlohmann::json out;
        if (m_method != Method::Saml)
        {
            out["success"] = false;
            out["error"] = "saml not selected";
        }
        else
        {
            std::string relayState;
            try
            {
                const auto req = nlohmann::json::parse(payloadToString(*msg));
                relayState = req.value("relay_state", "");
            }
            catch (const std::exception& e)
            {
                LOG_WARN("auth: bad saml start ({})", e.what());
            }

            const auto start = m_saml.buildAuthnRedirectUrl(relayState);
            out["success"] = start.success;
            if (start.success)
            {
                out["redirect_url"] = start.redirectUrl;
                out["request_id"] = start.requestId;
            }
            else
            {
                out["error"] = start.error;
            }
        }
        respond(serviceManager, AuthActionType::SendSamlStartResponse, dst, seqNo, out.dump());
        break;
    }

    case AuthEventType::ReceiveSamlAcsRequest:
    {
        nlohmann::json out;
        if (m_method != Method::Saml)
        {
            out["success"] = false;
            out["error"] = "saml not selected";
        }
        else
        {
            std::string samlResponse;
            try
            {
                const auto req = nlohmann::json::parse(payloadToString(*msg));
                samlResponse = req.value("saml_response", "");
            }
            catch (const std::exception& e)
            {
                LOG_WARN("auth: bad saml acs ({})", e.what());
            }

            const auto res = m_saml.verifyResponse(samlResponse);
            out["success"] = res.success;
            out["username"] = res.username;
            if (!res.success)
                out["error"] = res.error;
        }
        respond(serviceManager, AuthActionType::SendSamlAcsResponse, dst, seqNo, out.dump());
        break;
    }

    default:
        LOG_WARN("auth: unhandled event (type={})", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void AuthService::respond(AuthdServiceManager& serviceManager, AuthActionType type, pz::ipc::IpcDaemon dst,
                          std::uint32_t seqNo, const std::string& jsonPayload)
{
    serviceManager.postAction(std::make_unique<AuthAction>(type, dst, seqNo, jsonPayload));
}

void AuthService::handleAction(AuthdServiceManager& serviceManager, const AuthAction& action)
{
    pz::ipc::IpcCmd cmd = pz::ipc::IpcCmd::Unknown;
    switch (action.type())
    {
    case AuthActionType::SendLoginResponse:
        cmd = pz::ipc::IpcCmd::AuthLoginResponse;
        break;
    case AuthActionType::SendOidcStartResponse:
        cmd = pz::ipc::IpcCmd::AuthOidcStartResponse;
        break;
    case AuthActionType::SendOidcCallbackResponse:
        cmd = pz::ipc::IpcCmd::AuthOidcCallbackResponse;
        break;
    case AuthActionType::SendSamlStartResponse:
        cmd = pz::ipc::IpcCmd::AuthSamlStartResponse;
        break;
    case AuthActionType::SendSamlAcsResponse:
        cmd = pz::ipc::IpcCmd::AuthSamlAcsResponse;
        break;
    default:
        LOG_WARN("auth: unhandled action (type={})", static_cast<std::uint32_t>(action.type()));
        return;
    }

    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response);

    pz::ipc::IpcHeader header =
        pz::ipc::IpcHeader::build(pz::ipc::IpcDaemon::Authd, action.dst(), cmd, action.seqNo(), flag);

    const std::string& body = action.payload();
    std::vector<std::uint8_t> payload(body.begin(), body.end());

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header), std::move(payload));

    LOG_TRACE("auth: Tx {} (dst={}, seq={})", pz::ipc::IpcProtocol::cmdToStr(cmd),
              pz::ipc::IpcProtocol::daemonToStr(action.dst()), action.seqNo());

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

}
