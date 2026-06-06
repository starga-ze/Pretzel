#pragma once

#include <boost/beast/http.hpp>

#include <memory>
#include <string>

namespace nf::mgmtd
{

class AuthService;
class HttpCache;
class MetricService;

namespace http = boost::beast::http;

class HttpRouter
{
public:
    using Request = http::request<http::string_body>;
    using Response = http::response<http::string_body>;

    HttpRouter(MetricService* metricService,
               AuthService* authService,
               std::shared_ptr<HttpCache> cache);

    Response handle(const Request& req);

private:
    Response handleMetric(const Request& req);
    Response handleHealth(const Request& req);
    Response handleLogin(const Request& req);
    Response handleLogout(const Request& req);
    Response handleStatic(const Request& req);

    bool isStaticTarget(const std::string& target) const;
    std::string extractSession(const Request& req) const;

    static Response makeResponse(http::status status,
                                 std::string body,
                                 std::string contentType,
                                 unsigned version,
                                 bool keepAlive);

private:
    MetricService* m_metricService {nullptr};
    AuthService* m_authService {nullptr};
    std::shared_ptr<HttpCache> m_cache;
};

} // namespace nf::mgmtd
