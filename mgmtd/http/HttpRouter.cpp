#include "http/HttpRouter.h"

#include "http/HttpCache.h"
#include "service/auth/AuthService.h"
#include "service/metrics/MetricService.h"
#include "service/MgmtdServiceManager.h"
#include "router/MgmtdTxRouter.h"

#include "config/Config.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <csignal>
#include <fstream>
#include <unistd.h>
#include <utility>

namespace pz::mgmtd
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

    if (target == "/api/settings" && req.method() == http::verb::get)
    {
        if (!isAuthenticated(req))
        {
            return unauthorized(req.version(), req.keep_alive());
        }
        return handleSettingsGet(req);
    }

    if (target == "/api/settings" && req.method() == http::verb::post)
    {
        if (!isAuthenticated(req))
        {
            return unauthorized(req.version(), req.keep_alive());
        }
        return handleSettingsSet(req);
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
            res.set(http::field::server,   "pz-mgmtd");
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
    json body;

    body["uptime_seconds"] = 0.0;

    json daemons = json::array();

    if (m_serviceManager)
    {
        const auto alive = m_serviceManager->aliveDevices();
        body["alive_devices"] = alive.has_value() ? json(alive.value()) : json(nullptr);

        const auto& hb = m_serviceManager->heartbeatService();
        if (hb.hasData())
        {
            try
            {
                const auto hbRoot = json::parse(hb.latestJson());

                body["timestamp_ms"] = hbRoot.value("timestamp_ms", json(nullptr));

                for (const auto& d : hbRoot.value("daemons", json::array()))
                {
                    json entry;
                    entry["name"]       = d.value("name", "unknown");
                    entry["status"]     = d.value("status", "dead");
                    entry["latency_ms"] = d.contains("latency_ms") ? d["latency_ms"] : json(nullptr);
                    daemons.push_back(std::move(entry));
                }
            }
            catch (...)
            {
                // keep daemons array as-is on parse failure
            }
        }
    }
    else
    {
        body["alive_devices"] = nullptr;
    }

    // 3rd-party daemons are reported alongside the in-house daemons so the
    // dashboard can show every component (7 daemons + 3 3rd-party) in a single grid.
    daemons.push_back({{"name", "prometheus"},   {"status", "alive"}, {"latency_ms", nullptr}});
    daemons.push_back({{"name", "grafana"},      {"status", "alive"}, {"latency_ms", nullptr}});
    daemons.push_back({{"name", "node_exporter"},{"status", "alive"}, {"latency_ms", nullptr}});

    body["daemons"] = std::move(daemons);

    body["events"] = json::array({
        {{"source", "mgmtd"},  {"message", "HTTPS listener online"}},
        {{"source", "engined"}, {"message", "heartbeat polling active"}},
    });

    return makeResponse(http::status::ok,
                        body.dump(),
                        "application/json; charset=utf-8",
                        req.version(),
                        req.keep_alive());
}

namespace
{

// Daemons whose "tuning" section is exposed/editable through the settings
// dashboard. Mirrors the top-level keys in config/running-config.json.
constexpr const char* kSettingsDaemons[] = {
    "engined", "authd", "icmpd", "mgmtd", "snmpd", "topologyd", "ipcd",
};

} // namespace

HttpRouter::Response HttpRouter::handleSettingsGet(const Request& req)
{
    json daemons = json::object();

    for (const auto* name : kSettingsDaemons)
    {
        const auto& root = pz::config::Config::daemonConfig(name);
        daemons[name] = root.value("tuning", json::object());
    }

    json body;
    body["daemons"] = std::move(daemons);

    return makeResponse(http::status::ok,
                        body.dump(),
                        "application/json; charset=utf-8",
                        req.version(),
                        req.keep_alive());
}

HttpRouter::Response HttpRouter::handleSettingsSet(const Request& req)
{
    auto badRequest = [&](const char* error)
    {
        return makeResponse(http::status::bad_request,
                            json{{"error", error}}.dump(),
                            "application/json; charset=utf-8",
                            req.version(),
                            req.keep_alive());
    };

    json input;
    try
    {
        input = json::parse(req.body());
    }
    catch (const std::exception&)
    {
        return badRequest("invalid JSON body");
    }

    if (!input.contains("daemon") || !input.contains("domain") || !input.contains("values"))
    {
        return badRequest("expected {daemon, domain, values}");
    }

    const std::string daemon = input.value("daemon", "");
    const std::string domain = input.value("domain", "");
    const json& values = input["values"];

    if (!values.is_object())
    {
        return badRequest("'values' must be an object");
    }

    const bool knownDaemon = std::any_of(std::begin(kSettingsDaemons), std::end(kSettingsDaemons),
                                         [&](const char* d) { return daemon == d; });
    if (!knownDaemon)
    {
        return badRequest("unknown daemon");
    }

    if (!pz::config::Config::updateTuning(daemon, domain, values))
    {
        return makeResponse(http::status::internal_server_error,
                            json{{"error", "failed to persist settings"}}.dump(),
                            "application/json; charset=utf-8",
                            req.version(),
                            req.keep_alive());
    }

    LOG_INFO("Mgmtd settings updated daemon={} domain={} keys={}", daemon, domain, values.size());

    // Tell the running daemon to drop its cached tuning and reload from disk,
    // so the change applies without a restart. The daemon's pid is read from
    // its pid file (written at boot by pz::core::Core); SIGHUP delivery is
    // best-effort — the file write above already persisted the change, so a
    // missed signal only delays the reload until the daemon restarts.
    sendReloadSignal(daemon);

    return makeResponse(http::status::ok,
                        json{{"status", "ok"}}.dump(),
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
    auto pos = cookies.find(key);
    while (pos != std::string::npos)
    {
        if (pos == 0 || cookies[pos - 1] == ' ' || cookies[pos - 1] == ';')
            break;
        pos = cookies.find(key, pos + 1);
    }
    if (pos == std::string::npos) return {};

    auto end = cookies.find(';', pos);
    if (end == std::string::npos) end = cookies.size();

    return cookies.substr(pos + key.size(), end - (pos + key.size()));
}

void HttpRouter::sendReloadSignal(const std::string& daemon)
{
    const std::string pidFilePath = "/run/pretzel/" + daemon + ".pid";

    std::ifstream pidFile(pidFilePath);
    if (!pidFile)
    {
        LOG_WARN("Mgmtd reload: pid file not found for daemon={} path={}", daemon, pidFilePath);
        return;
    }

    pid_t pid = 0;
    pidFile >> pid;
    if (pid <= 0)
    {
        LOG_WARN("Mgmtd reload: invalid pid in {}", pidFilePath);
        return;
    }

    if (::kill(pid, SIGHUP) != 0)
    {
        LOG_WARN("Mgmtd reload: failed to send SIGHUP to daemon={} pid={}", daemon, pid);
        return;
    }

    LOG_INFO("Mgmtd reload: sent SIGHUP to daemon={} pid={}", daemon, pid);
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
    res.set(http::field::server,       "pz-mgmtd");
    res.set(http::field::content_type, std::move(contentType));
    res.keep_alive(keepAlive);
    res.body() = std::move(body);
    res.prepare_payload();
    return res;
}

} // namespace pz::mgmtd
