#pragma once

#include <boost/beast/http.hpp>

#include <memory>
#include <string>

namespace pz::mgmtd
{

class AuthService;
class HttpCache;
class MetricService;
class MgmtdServiceManager;

namespace http = boost::beast::http;

class HttpRouter
{
public:
    using Request  = http::request<http::string_body>;
    using Response = http::response<http::string_body>;

    HttpRouter(MetricService*             metricService,
               AuthService*               authService,
               MgmtdServiceManager*       serviceManager,
               std::shared_ptr<HttpCache> cache);

    Response handle(const Request& req);

private:
    Response handleMetric  (const Request& req);
    Response handleHealth  (const Request& req);
    Response handleLogin   (const Request& req);
    Response handleLogout  (const Request& req);
    Response handleStatus  (const Request& req);   // NEW
    Response handleSettingsGet    (const Request& req);
    // Accepts { changes: [{daemon, domain, values}] }, persists all updates,
    // broadcasts IpcCmd::ConfigReload to all daemons, and schedules mgmtd's
    // own restart. Returns per-change results.
    Response handleSettingsCommit (const Request& req);
    Response handleStatic         (const Request& req);

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
};

} // namespace pz::mgmtd
