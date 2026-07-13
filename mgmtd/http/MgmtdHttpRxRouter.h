#pragma once

#include "http/HttpHandler.h"

#include <boost/beast/http.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace pz::mgmtd
{

class AuthService;
class HttpCache;
class MetricService;
class MgmtdServiceManager;

namespace http = boost::beast::http;

// mgmtd's concrete HTTP handler: the admin/web REST API + static frontend. Plugs into
// the shared pz::http transport (listener/session) via the HttpHandler contract.
class MgmtdHttpRxRouter
{
public:
    using Request  = pz::http::HttpHandler::Request;
    using Response = pz::http::HttpHandler::Response;

    MgmtdHttpRxRouter(MetricService*             metricService,
               AuthService*               authService,
               MgmtdServiceManager*       serviceManager,
               std::shared_ptr<HttpCache> cache);

    Response dispatch(const Request& req);

private:
    Response handleMetric  (const Request& req);
    Response handleHealth  (const Request& req);
    Response handleLogin   (const Request& req);
    Response handleLogout  (const Request& req);
    Response handleChangePassword(const Request& req);
    Response handleWhoami  (const Request& req);
    Response handleStatus  (const Request& req);   // NEW
    Response handleSettingsGet      (const Request& req);
    Response handleSettingsCommit   (const Request& req);
    Response handleReloadStatus     (const Request& req);
    Response handleCommitQueue      (const Request& req);
    Response handleDevices          (const Request& req);
    Response handleLogs             (const Request& req);
    Response handleNodeMetrics      (const Request& req);
    Response handleLabExport        (const Request& req);   // canvas image → server-served attachment
    Response handleStatic           (const Request& req);

    // ── SSO (Okta via authd) ──────────────────────────────────────────────
    Response handleSsoInfo   (const Request& req);   // is federated login configured?
    Response handleSsoLogin  (const Request& req);   // SP-initiated: POST AuthnRequest to IdP
    Response handleSamlAcs   (const Request& req);   // IdP POSTs SAMLResponse here
    Response handleSamlResult(const Request& req);   // browser polls for the authd verdict

    // Returns true when the request carries a valid session cookie.
    bool     isAuthenticated(const Request& req) const;
    bool     isStaticTarget (const std::string& target) const;
    std::string extractSession(const Request& req) const;

    static Response makeResponse(http::status status,
                                 std::string  body,
                                 std::string  contentType,
                                 unsigned     version,
                                 bool         keepAlive);

    // Shorthand for 401 Unauthorized
    static Response unauthorized(unsigned version, bool keepAlive);

private:
    MetricService*             m_metricService    {nullptr};
    AuthService*               m_authService      {nullptr};
    MgmtdServiceManager*       m_serviceManager   {nullptr};
    std::shared_ptr<HttpCache> m_cache;

    // Monotonic ticket for correlating an ACS request with authd's async response
    // (used as the IPC seqNo). Single main-loop thread, so a plain counter is safe.
    std::uint32_t              m_ssoTicket        {1};
};

} // namespace pz::mgmtd
