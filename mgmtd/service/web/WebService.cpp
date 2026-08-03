#include "service/web/WebService.h"

#include "service/MgmtdServiceManager.h"

#include "service/web/WebAction.h"
#include "service/web/WebEvent.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

#include "util/Logger.h"

#include <memory>
#include <string>
#include <utility>

namespace pz::mgmtd
{

namespace
{

constexpr const char* kPublicPages[] = {
    "/", "/index.html", "/css/main.css", "/js/main.js", "/js/login.js",
};

}

void WebService::handleEvent(MgmtdServiceManager& sm, const WebEvent& event)
{
    Response resp;
    route(sm, event.request(), resp);

    sm.postAction(std::make_unique<WebAction>(std::move(resp), event.sessionId()));
}

void WebService::handleAction(MgmtdServiceManager& sm, WebAction& action)
{
    sm.txRouter().handleHttpMessage(std::move(action.response()), action.sessionId());
}

// ── The route table ────────────────────────────────────────────────────────────────────────────
// One row per URL, in first-match order. Data only — no function pointers: the row names a WebRoute,
// and route() switches on it. Adding an endpoint is one row here plus one case in the switch, the
// same two-step a new ApiEventType is in collectord.
WebService::Resolved WebService::resolve(const std::string& method, const std::string& target) const
{
    struct Row
    {
        const char* method;
        const char* path;
        Match match;
        WebRoute route;
        Access access;
        bool mustChangeExempt;
    };

    // clang-format off
    static const Row kRoutes[] = {
        // Public.
        {"GET",  "/metrics",                            
            Match::Exact,  WebRoute::Metric,             Access::Public,        false},
        {"GET",  "/health",                             
            Match::Exact,  WebRoute::Health,             Access::Public,        false},

        // Auth — local password sign-in. change-password is authenticated but exempt from the
        // must-change lock: it is the escape from it.
        {"POST", "/api/login",                          
            Match::Exact,  WebRoute::Login,              Access::Public,        false},
        {"POST", "/api/logout",                         
            Match::Exact,  WebRoute::Logout,             Access::Public,        false},
        {"POST", "/api/change-password",                
            Match::Exact,  WebRoute::ChangePassword,     Access::Authenticated, true},
        {"GET",  "/api/whoami",                         
            Match::Exact,  WebRoute::Whoami,             Access::Authenticated, false},

        // SSO / SAML.
        {"GET",  "/api/auth/sso/info",                  
            Match::Exact,  WebRoute::SsoInfo,            Access::Public,        false},
        {"GET",  "/api/auth/sso/login",                 
            Match::Exact,  WebRoute::SsoLogin,           Access::Public,        false},
        {"POST", "/api/auth/saml/acs",                  
            Match::Exact,  WebRoute::SamlAcs,            Access::Public,        false},
        {"GET",  "/api/auth/saml/result",               
            Match::Prefix, WebRoute::SamlResult,         Access::Public,        false},

        // Settings.
        {"GET",  "/api/settings",                       
            Match::Exact,  WebRoute::Settings,           Access::Authenticated, false},
        {"GET",  "/api/settings/running-config",        
            Match::Exact,  WebRoute::RunningConfig,      Access::Authenticated, false},
        {"POST", "/api/settings/commit",                
            Match::Exact,  WebRoute::SettingsCommit,     Access::Authenticated, false},
        {"GET",  "/api/settings/reload-status",         
            Match::Exact,  WebRoute::ReloadStatus,       Access::Authenticated, false},
        {"GET",  "/api/settings/commit-queue",          
            Match::Exact,  WebRoute::CommitQueue,        Access::Authenticated, false},
        {"POST", "/api/settings/save-config",           
            Match::Exact,  WebRoute::SaveConfig,         Access::Authenticated, false},
        {"GET",  "/api/settings/saved-configs",         
            Match::Exact,  WebRoute::SavedConfigs,       Access::Authenticated, false},
        {"GET",  "/api/settings/saved-config-content",  
            Match::Prefix, WebRoute::SavedConfigContent, Access::Authenticated, false},

        // Status.
        {"GET",  "/api/status/devices",                 
            Match::Exact,  WebRoute::DeviceStatus,       Access::Authenticated, false},

        // Topology.
        {"GET",  "/api/topology",                       Match::Exact,  WebRoute::SiteTopology,       Access::Authenticated, false},

        // Connector tests + credential state.
        {"POST", "/api/connector/keygen-test",          
            Match::Exact,  WebRoute::KeygenTest,         Access::Authenticated, false},
        {"POST", "/api/connector/endpoint-test",        
            Match::Exact,  WebRoute::EndpointTest,       Access::Authenticated, false},
        {"POST", "/api/connector/sase-test",            
            Match::Exact,  WebRoute::SaseTest,           Access::Authenticated, false},
        {"POST", "/api/connector/sase-key",             
            Match::Exact,  WebRoute::SaseKeyStore,       Access::Authenticated, false},
        {"POST", "/api/connector/credential",           
            Match::Exact,  WebRoute::CredentialStore,    Access::Authenticated, false},
        {"POST", "/api/connector/tls-probe",            
            Match::Exact,  WebRoute::TlsProbe,           Access::Authenticated, false},
        {"GET",  "/api/connector/test-result",          
            Match::Prefix, WebRoute::ApiTestResult,      Access::Authenticated, false},
        {"GET",  "/api/connector/keys-state",           
            Match::Exact,  WebRoute::KeysState,          Access::Authenticated, false},

        // Logs.
        {"GET",  "/api/logs",                           
            Match::Prefix, WebRoute::Logs,               Access::Authenticated, false},
    };
    // clang-format on

    for (const auto& r : kRoutes)
    {
        if (method != r.method)
            continue;
        const bool hit =
            (r.match == Match::Exact) ? (target == r.path) : (target.rfind(r.path, 0) == 0);
        if (hit)
            return Resolved{r.route, r.access, r.mustChangeExempt};
    }
    return Resolved{};
}

// Resolve, apply the two cross-cutting gates once, then hand off to the owning controller in a single
// switch — the mgmtd counterpart of collectord's ApiService::route().
void WebService::route(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    LOG_TRACE("HTTP request (method={}, target={})", req.method, req.target);

    const Resolved r = resolve(req.method, req.target);
    if (r.route == WebRoute::None)
    {
        // Nothing in the table claimed it — a static file (everything outside /api).
        if (isStaticTarget(req.target))
            staticFallback(sm, req, resp);
        return;
    }

    if (r.access == Access::Authenticated)
    {
        if (!sm.authService().validateSession(sessionCookie(req)))
        {
            // The code lets the frontend tell a session-expiry 401 (bounce to login) apart from a
            // semantic 401 like a wrong current password on the change-password form.
            return fill(resp, 401, R"({"error":"unauthorized","code":"UNAUTHENTICATED"})");
        }

        // The must-change-password lock: an authenticated operator with a pending forced change may
        // reach only the change-password route until it is done.
        if (!r.mustChangeExempt && req.target.rfind("/api/", 0) == 0 &&
            sm.authService().mustChangePassword())
        {
            return fill(resp, 403, R"({"error":"password change required","code":"MUST_CHANGE_PASSWORD"})");
        }
    }

    switch (r.route)
    {
    case WebRoute::Metric:             return metric(sm, req, resp);
    case WebRoute::Health:             return health(sm, req, resp);

    case WebRoute::Login:              return m_authController.login(sm, req, resp);
    case WebRoute::Logout:             return m_authController.logout(sm, req, resp);
    case WebRoute::ChangePassword:     return m_authController.changePassword(sm, req, resp);
    case WebRoute::Whoami:             return m_authController.whoami(sm, req, resp);

    case WebRoute::SsoInfo:            return m_ssoController.info(sm, req, resp);
    case WebRoute::SsoLogin:           return m_ssoController.login(sm, req, resp);
    case WebRoute::SamlAcs:            return m_ssoController.samlAcs(sm, req, resp);
    case WebRoute::SamlResult:         return m_ssoController.samlResult(sm, req, resp);

    case WebRoute::Settings:           return m_settingsController.get(sm, req, resp);
    case WebRoute::RunningConfig:      return m_settingsController.runningConfig(sm, req, resp);
    case WebRoute::SettingsCommit:     return m_settingsController.commit(sm, req, resp);
    case WebRoute::ReloadStatus:       return m_settingsController.reloadStatus(sm, req, resp);
    case WebRoute::CommitQueue:        return m_settingsController.commitQueue(sm, req, resp);
    case WebRoute::SaveConfig:         return m_settingsController.saveConfig(sm, req, resp);
    case WebRoute::SavedConfigs:       return m_settingsController.savedConfigs(sm, req, resp);
    case WebRoute::SavedConfigContent: return m_settingsController.savedConfigContent(sm, req, resp);

    case WebRoute::DeviceStatus:       return m_statusController.deviceStatus(sm, req, resp);
    case WebRoute::SiteTopology:       return m_statusController.siteTopology(sm, req, resp);

    case WebRoute::KeygenTest:         return m_apiController.keygenTest(sm, req, resp);
    case WebRoute::EndpointTest:       return m_apiController.endpointTest(sm, req, resp);
    case WebRoute::SaseTest:           return m_apiController.saseTest(sm, req, resp);
    case WebRoute::SaseKeyStore:       return m_apiController.saseKeyStore(sm, req, resp);
    case WebRoute::CredentialStore:    return m_apiController.credentialStore(sm, req, resp);
    case WebRoute::TlsProbe:           return m_apiController.tlsProbe(sm, req, resp);
    case WebRoute::ApiTestResult:      return m_apiController.testResult(sm, req, resp);
    case WebRoute::KeysState:          return m_apiController.keysState(sm, req, resp);

    case WebRoute::Logs:               return m_logsController.list(sm, req, resp);

    case WebRoute::None:               break;   // unreachable — filtered above
    }
}

void WebService::metric(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    fill(resp, 200, sm.metricService().renderPrometheus(), "text/plain; version=0.0.4; charset=utf-8");
}

// Unauthenticated liveness probe. Always 200 while mgmtd is serving — reaching this handler is itself
// proof of that — with the body carrying engined's latest per-daemon heartbeat roll-up
// ({timestamp_ms, daemons:[{name,status,latency_ms}]}) so the Home page can render a status grid.
// Until the first heartbeat round lands, the daemon list is empty rather than stale. Public route.
void WebService::health(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    auto& hb = sm.heartbeatService();
    if (hb.hasData())
        fill(resp, 200, hb.latestJson());
    else
        fill(resp, 200, R"({"timestamp_ms":0,"daemons":[]})");
}

void WebService::staticFallback(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    bool isPublic = false;
    for (const auto* p : kPublicPages)
    {
        if (req.target == p)
        {
            isPublic = true;
            break;
        }
    }

    if (!isPublic && !sm.authService().validateSession(sessionCookie(req)))
    {
        resp.status = 302;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body.clear();
        resp.location = "/index.html";
        return;
    }

    serveStatic(sm, req, resp);
}

void WebService::serveStatic(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    const auto& cache = sm.staticCache();
    if (!cache)
        return fill(resp, 503, "static cache unavailable", "text/plain; charset=utf-8");

    std::string staticPath(req.target);
    const auto qpos = staticPath.find('?');
    if (qpos != std::string::npos)
        staticPath.erase(qpos);

    auto file = cache->get(staticPath);
    if (!file)
        return fill(resp, 404, "not found", "text/plain; charset=utf-8");

    fill(resp, 200, std::move(file->body), std::move(file->contentType));
    resp.etag = std::move(file->etag);
}

bool WebService::isStaticTarget(const std::string& target) const
{
    return target.rfind("/api", 0) != 0;
}

}
