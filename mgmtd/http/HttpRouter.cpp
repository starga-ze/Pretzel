#include "http/HttpRouter.h"

#include "http/HttpCache.h"
#include "service/auth/AuthService.h"
#include "service/metrics/MetricService.h"
#include "service/MgmtdServiceManager.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace nf::mgmtd
{

using json = nlohmann::json;

// Static pages that do NOT require a session.
// Everything else under "/" is protected.
static constexpr const char* kPublicPages[] = {
    "/",
    "/index.html",
    "/css/main.css",
    "/js/main.js",
    "/js/login.js",  // 로그인 페이지에서 세션 없이 로드되어야 함
};

HttpRouter::HttpRouter(MetricService*             metricService,
                       AuthService*               authService,
                       MgmtdServiceManager*       serviceManager,
                       std::shared_ptr<HttpCache> cache)
    : m_metricService(metricService),
      m_authService(authService),
      m_serviceManager(serviceManager),
      m_cache(std::move(cache))
{
}

HttpRouter::Response HttpRouter::handle(const Request& req)
{
    const std::string target(req.target());

    LOG_INFO("Mgmtd HTTP request method={} target={}", req.method_string(), target);

    // ── Public API routes (no auth required) ──────────────────────────────
    if (target == "/metrics" && req.method() == http::verb::get)
    {
        return handleMetric(req);
    }

    if (target == "/health" && req.method() == http::verb::get)
    {
        return handleHealth(req);
    }

    if (target == "/api/login" && req.method() == http::verb::post)
    {
        return handleLogin(req);
    }

    // ── Auth-required API routes ──────────────────────────────────────────
    if (target == "/api/logout" && req.method() == http::verb::post)
    {
        // logout is safe to call even without a valid session
        return handleLogout(req);
    }

    if (target == "/api/status" && req.method() == http::verb::get)
    {
        if (!isAuthenticated(req))
        {
            return unauthorized(req.version(), req.keep_alive());
        }
        return handleStatus(req);
    }

    // ── Static files ──────────────────────────────────────────────────────
    if (isStaticTarget(target))
    {
        // Allow public pages without session; protect everything else.
        bool isPublic = false;
        for (const auto* p : kPublicPages)
        {
            if (target == p) { isPublic = true; break; }
        }

        if (!isPublic && !isAuthenticated(req))
        {
            // Redirect browsers to login page instead of a bare 401.
            Response res{http::status::found, req.version()};
            res.set(http::field::server,   "nf-mgmtd");
            res.set(http::field::location, "/index.html");
            res.keep_alive(req.keep_alive());
            res.prepare_payload();
            return res;
        }

        return handleStatic(req);
    }

    return makeResponse(http::status::not_found,
                        R"({"error":"not found"})",
                        "application/json; charset=utf-8",
                        req.version(),
                        req.keep_alive());
}

// ── Handlers ─────────────────────────────────────────────────────────────────

HttpRouter::Response HttpRouter::handleMetric(const Request& req)
{
    if (!m_metricService)
    {
        return makeResponse(http::status::service_unavailable,
                            "# mgmtd metric service unavailable\n",
                            "text/plain; version=0.0.4; charset=utf-8",
                            req.version(),
                            req.keep_alive());
    }

    return makeResponse(http::status::ok,
                        m_metricService->renderPrometheus(),
                        "text/plain; version=0.0.4; charset=utf-8",
                        req.version(),
                        req.keep_alive());
}

HttpRouter::Response HttpRouter::handleHealth(const Request& req)
{
    return makeResponse(http::status::ok,
                        R"({"status":"ok","daemon":"mgmtd"})",
                        "application/json; charset=utf-8",
                        req.version(),
                        req.keep_alive());
}

HttpRouter::Response HttpRouter::handleLogin(const Request& req)
{
    if (!m_authService)
    {
        return makeResponse(http::status::service_unavailable,
                            R"({"error":"auth unavailable"})",
                            "application/json; charset=utf-8",
                            req.version(),
                            req.keep_alive());
    }

    try
    {
        const auto body     = json::parse(req.body());
        const auto username = body.at("username").get<std::string>();
        const auto password = body.at("password").get<std::string>();

        const auto result = m_authService->login(username, password);
        if (!result.success)
        {
            return makeResponse(http::status::unauthorized,
                                R"({"error":"invalid credentials"})",
                                "application/json; charset=utf-8",
                                req.version(),
                                req.keep_alive());
        }

        auto res = makeResponse(http::status::ok,
                                R"({"status":"ok"})",
                                "application/json; charset=utf-8",
                                req.version(),
                                req.keep_alive());
        res.set(http::field::set_cookie,
                "session=" + result.sessionId + "; HttpOnly; Path=/; SameSite=Strict");
        return res;
    }
    catch (const std::exception& e)
    {
        LOG_WARN("Mgmtd login bad request: {}", e.what());
        return makeResponse(http::status::bad_request,
                            R"({"error":"bad request"})",
                            "application/json; charset=utf-8",
                            req.version(),
                            req.keep_alive());
    }
}

HttpRouter::Response HttpRouter::handleLogout(const Request& req)
{
    if (m_authService)
    {
        m_authService->logout(extractSession(req));
    }

    auto res = makeResponse(http::status::ok,
                            R"({"status":"logged_out"})",
                            "application/json; charset=utf-8",
                            req.version(),
                            req.keep_alive());
    res.set(http::field::set_cookie, "session=; Path=/; Max-Age=0");
    return res;
}

HttpRouter::Response HttpRouter::handleStatus(const Request& req)
{
    // Build a JSON status blob that dashboard.js can consume.
    // Fields mirror what the frontend expects:
    //   management.status, uptime_seconds, prometheus.status,
    //   node_exporter.status, alive_devices, daemons[], events[]
    //
    // Real daemon/IPC data would be injected here once wired up.
    // For now we derive what we can from MetricService metrics and
    // return sensible placeholder values for the rest.

    json body;

    // Management
    body["management"]["status"] = "Active";
    body["management"]["sub"]    = "HTTPS listener online";

    // Uptime — read from MetricService if available
    double uptimeSec = 0.0;
    if (m_metricService)
    {
        // renderPrometheus exposes pretzel_mgmtd_uptime_seconds;
        // we expose it directly here for the dashboard.
        // TODO: add MetricService::uptimeSeconds() accessor instead of
        //       parsing the Prometheus text format.
        uptimeSec = 0.0; // placeholder until accessor is added
    }
    body["uptime_seconds"] = uptimeSec;

    // Prometheus connectivity (we can reach /metrics so it's running)
    body["prometheus"]["status"] = "Connected";

    // Node Exporter — unknown until IPC reports it
    body["node_exporter"]["status"] = "Pending";

    // Alive devices — icmpd ProbeResult 수신 시 갱신, 미수신이면 null
    if (m_serviceManager)
    {
        const auto alive = m_serviceManager->aliveDevices();
        body["alive_devices"] = alive.has_value() ? json(alive.value()) : json(nullptr);
    }
    else
    {
        body["alive_devices"] = nullptr;
    }

    // Daemon status — will be populated via IPC in the future
    body["daemons"] = json::array({
        {{"name", "nf-ipcd"},  {"status", "UP"}},
        {{"name", "nf-mgmtd"}, {"status", "UP"}},
        {{"name", "nf-icmpd"}, {"status", "UP"}},
        {{"name", "nf-snmpd"}, {"status", "WAIT"}},
    });

    // Events — last N system events (placeholder)
    body["events"] = json::array({
        {{"source", "mgmtd"},      {"message", "metrics scrape completed"}},
        {{"source", "icmpd"},      {"message", "waiting probe cycle"}},
        {{"source", "prometheus"}, {"message", "target healthy"}},
    });

    return makeResponse(http::status::ok,
                        body.dump(),
                        "application/json; charset=utf-8",
                        req.version(),
                        req.keep_alive());
}

HttpRouter::Response HttpRouter::handleStatic(const Request& req)
{
    if (!m_cache)
    {
        return makeResponse(http::status::service_unavailable,
                            "static cache unavailable",
                            "text/plain; charset=utf-8",
                            req.version(),
                            req.keep_alive());
    }

    auto file = m_cache->get(std::string(req.target()));
    if (!file)
    {
        return makeResponse(http::status::not_found,
                            "not found",
                            "text/plain; charset=utf-8",
                            req.version(),
                            req.keep_alive());
    }

    return makeResponse(http::status::ok,
                        std::move(file->body),
                        file->contentType,
                        req.version(),
                        req.keep_alive());
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool HttpRouter::isAuthenticated(const Request& req) const
{
    if (!m_authService) return false;
    return m_authService->validateSession(extractSession(req));
}

bool HttpRouter::isStaticTarget(const std::string& target) const
{
    return target.rfind("/api", 0) != 0;
}

std::string HttpRouter::extractSession(const Request& req) const
{
    auto it = req.find(http::field::cookie);
    if (it == req.end()) return {};

    const std::string cookies(it->value());
    const std::string key = "session=";
    const auto pos = cookies.find(key);
    if (pos == std::string::npos) return {};

    auto end = cookies.find(';', pos);
    if (end == std::string::npos) end = cookies.size();

    return cookies.substr(pos + key.size(), end - (pos + key.size()));
}

HttpRouter::Response HttpRouter::unauthorized(unsigned version, bool keepAlive)
{
    return makeResponse(http::status::unauthorized,
                        R"({"error":"unauthorized"})",
                        "application/json; charset=utf-8",
                        version,
                        keepAlive);
}

HttpRouter::Response HttpRouter::makeResponse(http::status status,
                                              std::string  body,
                                              std::string  contentType,
                                              unsigned     version,
                                              bool         keepAlive)
{
    Response res{status, version};
    res.set(http::field::server,       "nf-mgmtd");
    res.set(http::field::content_type, std::move(contentType));
    res.keep_alive(keepAlive);
    res.body() = std::move(body);
    res.prepare_payload();
    return res;
}

} // namespace nf::mgmtd
