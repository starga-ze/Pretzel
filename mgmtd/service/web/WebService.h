#pragma once

#include "service/web/controller/ApiController.h"
#include "service/web/controller/AuthController.h"
#include "service/web/controller/CollectionController.h"
#include "service/web/controller/LogsController.h"
#include "service/web/controller/SettingsController.h"
#include "service/web/controller/SsoController.h"
#include "service/web/controller/StatusController.h"

#include "http/HttpMessage.h"

#include <string>

namespace pz::mgmtd
{

class MgmtdServiceManager;
class WebEvent;
class WebAction;

// Every URL this service answers, as one enum value — the switch key WebService::route() dispatches
// on. This mirrors collectord's ApiEventType: routing is a direct switch to the owning controller, no
// function-pointer table. `None` means nothing matched, so the caller falls back to static serving.
enum class WebRoute
{
    None = 0,

    // WebService itself (public).
    Metric,   // GET  /metrics
    Health,   // GET  /health

    // AuthController — local password sign-in.
    Login,            // POST /api/login
    Logout,           // POST /api/logout
    ChangePassword,   // POST /api/change-password  (authenticated, must-change-exempt)
    Whoami,           // GET  /api/whoami

    // SsoController — SAML sign-in.
    SsoInfo,      // GET  /api/auth/sso/info
    SsoLogin,     // GET  /api/auth/sso/login
    SamlAcs,      // POST /api/auth/saml/acs
    SamlResult,   // GET  /api/auth/saml/result?ticket=

    // SettingsController — configuration surface.
    Settings,             // GET  /api/settings
    RunningConfig,        // GET  /api/settings/running-config
    SettingsCommit,       // POST /api/settings/commit
    ReloadStatus,         // GET  /api/settings/reload-status
    CommitQueue,          // GET  /api/settings/commit-queue
    SaveConfig,           // POST /api/settings/save-config
    SavedConfigs,         // GET  /api/settings/saved-configs
    SavedConfigContent,   // GET  /api/settings/saved-config-content?name=

    // StatusController.
    DeviceStatus,   // GET  /api/status/devices
    SiteTopology,   // GET  /api/topology

    // ApiController — connector tests + credential state.
    KeygenTest,        // POST /api/connector/keygen-test
    EndpointTest,      // POST /api/connector/endpoint-test
    SaseTest,          // POST /api/connector/sase-test
    SaseKeyStore,      // POST /api/connector/sase-key
    CredentialStore,   // POST /api/connector/credential
    TlsProbe,          // POST /api/connector/tls-probe
    ApiTestResult,     // GET  /api/connector/test-result?ticket=
    KeysState,         // GET  /api/connector/keys-state

    // CollectionController — the API collection pipeline's read side.
    CollectionOverview,   // GET  /api/collection/overview?window=
    CollectionSamples,    // GET  /api/collection/samples?connector=&endpoint=&status=&before=&limit=
    CollectionSample,     // GET  /api/collection/sample?oid=

    // LogsController.
    Logs,   // GET  /api/logs?…
};

// The mgmtd HTTP entry point. On each WebEvent it resolves (method, target) to a WebRoute, applies
// the two cross-cutting gates ONCE — authentication and the must-change-password lock — then hands
// off to the owning controller in a single switch. That central gate is what lets a controller be
// pure request→response glue: none of them re-check auth.
//
// It runs on the service loop, not the transport thread: MgmtdRxRouter only wraps an inbound request
// into a WebEvent (crossing the thread boundary); the URL routing happens here when the event is
// processed. The controllers are owned here as instances (no static handlers), mirroring how
// collectord's ApiService owns its connector-test controllers.
class WebService
{
public:
    WebService() = default;

    void handleEvent(MgmtdServiceManager& serviceManager, const WebEvent& event);

    void handleAction(MgmtdServiceManager& serviceManager, WebAction& action);

private:
    using Request = pz::http::HttpRequest;
    using Response = pz::http::HttpResponse;

    enum class Access
    {
        Public,          // no session required (login, health, SAML ACS, …)
        Authenticated,   // a valid session is required
    };

    enum class Match
    {
        Exact,    // req.target == path
        Prefix,   // req.target starts with path (query string or sub-path follows)
    };

    // The outcome of matching a request against the route table: which route, and the two gate
    // policies that apply to it. `mustChangeExempt` marks a route that still needs a session but must
    // not be blocked by the must-change-password lock — only /api/change-password, the escape from it.
    struct Resolved
    {
        WebRoute route{WebRoute::None};
        Access access{Access::Authenticated};
        bool mustChangeExempt{false};
    };

    // Resolve (method, target) against the static route table — first match wins, same order the
    // routes are declared. Returns {WebRoute::None, …} when nothing matched.
    Resolved resolve(const std::string& method, const std::string& target) const;

    // Match, gate, dispatch. The one place the auth and must-change gates live.
    void route(MgmtdServiceManager& sm, const Request& req, Response& resp);

    // WebService's own routes; every other domain lives in a controller below.
    void metric(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void health(MgmtdServiceManager& sm, const Request& req, Response& resp);

    // Non-/api targets that no route claimed: serve the file, redirecting to the login page when the
    // page is not public and the caller has no session.
    void staticFallback(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void serveStatic(MgmtdServiceManager& sm, const Request& req, Response& resp);

    bool isStaticTarget(const std::string& target) const;

    // The domain controllers, owned as instances (stateless — each reads what it needs from the
    // service manager). Reached from the dispatch switch by member call, not a function pointer.
    AuthController m_authController;
    SsoController m_ssoController;
    SettingsController m_settingsController;
    StatusController m_statusController;
    ApiController m_apiController;
    CollectionController m_collectionController;
    LogsController m_logsController;
};

}
