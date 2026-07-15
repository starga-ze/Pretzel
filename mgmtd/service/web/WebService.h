#pragma once

#include "http/HttpMessage.h"
#include "http/StaticFileCache.h"

#include <cstdint>
#include <memory>
#include <string>

namespace pz::mgmtd
{

class MgmtdServiceManager;
class WebEvent;
class WebAction;

// Web/admin domain logic: the REST API + static frontend that back the mgmtd GUI. It
// handles inbound HTTP events (dispatched through the ServiceManager queue, same as IPC
// events): it routes by method/target, enforces session auth, and fills the event's
// response slot. Cross-daemon calls (authd SAML verify, engined config/password writes)
// go out through the ServiceManager's IPC TxRouter.
//
// Transport-agnostic — it never sees boost::beast; it reads a HttpRequest and writes a
// HttpResponse. Runs on the single poll thread, so its per-request state needs no locking.
class WebService
{
public:
    WebService() = default;

    // Injected after construction (the static-asset cache is built by the Core from the
    // share dir). Safe to leave unset — handleStatic 503s when the cache is absent.
    void setCache(std::shared_ptr<pz::http::StaticFileCache> cache);

    // Routes the request, fills a response, and posts a WebAction (Event -> Action).
    void handleEvent(MgmtdServiceManager& serviceManager, const WebEvent& event);

    // Drains the WebAction: performs the egress via the TxRouter (Action -> TxRouter ->
    // HttpHandler::egress -> session write), the exact analogue of BootstrapService::handleAction
    // doing IPC egress. Non-const action so the response body can be moved out, not copied.
    void handleAction(MgmtdServiceManager& serviceManager, WebAction& action);

private:
    using Request  = pz::http::HttpRequest;
    using Response = pz::http::HttpResponse;

    // Pure routing: fill resp from req (no transport, no queue). Split out so handleEvent
    // dispatches the response for egress regardless of which branch produced it.
    void route(MgmtdServiceManager& sm, const Request& req, Response& resp);

    void handleMetric         (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleHealth         (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleLogin          (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleLogout         (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleChangePassword (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleWhoami         (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleStatus         (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSettingsGet    (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSettingsCommit (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleReloadStatus   (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleCommitQueue    (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleDevices        (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleLogs           (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleNodeMetrics    (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleLabExport      (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleStatic         (MgmtdServiceManager& sm, const Request& req, Response& resp);

    // ── SSO (Okta via authd) ──────────────────────────────────────────────
    void handleSsoInfo        (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSsoLogin       (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSamlAcs        (MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSamlResult     (MgmtdServiceManager& sm, const Request& req, Response& resp);

    // Returns true when the request carries a valid session cookie.
    bool isAuthenticated(MgmtdServiceManager& sm, const Request& req) const;

    static bool        isStaticTarget(const std::string& target);
    static std::string extractSession(const Request& req);

    std::shared_ptr<pz::http::StaticFileCache> m_cache;

    // Monotonic ticket for correlating an ACS request with authd's async response
    // (used as the IPC seqNo). Single poll thread, so a plain counter is safe.
    std::uint32_t m_ssoTicket{1};
};

} // namespace pz::mgmtd
