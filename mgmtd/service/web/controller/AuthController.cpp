#include "service/web/controller/AuthController.h"

#include "service/MgmtdServiceManager.h"

#include "service/web/WebResponse.h"
#include "service/web/WebRouter.h"

#include "router/MgmtdTxRouter.h"

#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace pz::mgmtd
{

namespace
{

using json = nlohmann::json;

// Hands a new local credential to engined, the only database writer. Shared by the explicit
// password change and by the transparent upgrade a login performs when it meets an
// old-format hash.
void persistCredential(MgmtdServiceManager& sm, const std::string& username,
                       const AuthService::Credential& cred)
{
    const json payload = {{"username", username}, {"password_hash", cred.passwordHash}, {"salt", cred.salt}};
    const std::string payloadStr = payload.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::AdminPasswordUpdate);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payloadStr.begin(), payloadStr.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

void handleLogin(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    try
    {
        const auto body = json::parse(req.body);
        const auto username = body.at("username").get<std::string>();
        const auto password = body.at("password").get<std::string>();

        const auto result = sm.authService().login(username, password);
        if (result.throttled)
            return fill(resp, 429, R"({"error":"too many failed attempts — try again shortly"})");
        if (!result.success)
            return fill(resp, 401, R"({"error":"invalid credentials"})");

        // The credential verified against an outdated hash format. This is the only moment the
        // plaintext exists, so it is re-stored now rather than left to expire on its own — the
        // operator sees nothing, and the old-format row is gone after one login.
        if (result.rehashNeeded)
        {
            const auto cred = sm.authService().makeCredential(password);
            if (cred.passwordHash.empty())
            {
                LOG_WARN("credential upgrade skipped — rehash failed (user={})", username);
            }
            else
            {
                persistCredential(sm, username, cred);
                sm.authService().applyCredential(cred.passwordHash, cred.salt);
                LOG_INFO("credential upgraded to the current hash format (user={})", username);
            }
        }

        const json okBody = {{"status", "ok"}, {"must_change", result.mustChange}};
        fill(resp, 200, okBody.dump());
        resp.setCookie = "session=" + result.sessionId + "; HttpOnly; Path=/; SameSite=Strict";
    }
    catch (const std::exception& e)
    {
        LOG_WARN("login bad request (error={})", e.what());
        fill(resp, 400, R"({"error":"bad request"})");
    }
}

void handleLogout(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    sm.authService().logout(sessionCookie(req));

    fill(resp, 200, R"({"status":"logged_out"})");
    resp.setCookie = "session=; Path=/; Max-Age=0";
}

void handleChangePassword(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    try
    {
        const auto body = json::parse(req.body);
        const auto oldPass = body.at("old_password").get<std::string>();
        const auto newPass = body.at("new_password").get<std::string>();

        if (newPass.empty())
            return fill(resp, 400, R"({"error":"new password must not be empty"})");

        const std::string& user = sm.authService().username();
        if (!sm.authService().checkPassword(user, oldPass))
            return fill(resp, 401, R"({"error":"current password is incorrect"})");

        const auto cred = sm.authService().makeCredential(newPass);
        if (cred.passwordHash.empty())
        {
            // The CSPRNG or the KDF failed. Reporting success here would leave the old password
            // in place while telling the operator it had changed.
            LOG_ERROR("password change aborted — credential could not be generated (user={})", user);
            return fill(resp, 500, R"({"error":"could not generate the new credential"})");
        }

        persistCredential(sm, user, cred);
        sm.authService().applyCredential(cred.passwordHash, cred.salt);

        LOG_INFO("admin password change sent to engined (user={})", user);
        fill(resp, 200, R"({"status":"ok"})");
    }
    catch (const std::exception& e)
    {
        LOG_WARN("change-password bad request (error={})", e.what());
        fill(resp, 400, R"({"error":"bad request"})");
    }
}

void handleWhoami(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    std::string user = sm.authService().sessionUser(sessionCookie(req));
    if (user.empty())
        user = sm.authService().username();

    const json out = {{"username", user}};
    fill(resp, 200, out.dump());
}

}

void AuthController::registerRoutes(WebRouter& router)
{
    using Access = WebRouter::Access;

    router.post("/api/login", Access::Public, &handleLogin);
    router.post("/api/logout", Access::Public, &handleLogout);

    // change-password is Authenticated but exempt from the must-change lock — it is the escape from it.
    router.add("POST", "/api/change-password", WebRouter::Match::Exact, Access::Authenticated,
               /*mustChangeExempt=*/true, &handleChangePassword);
    router.get("/api/whoami", Access::Authenticated, &handleWhoami);
}

}
