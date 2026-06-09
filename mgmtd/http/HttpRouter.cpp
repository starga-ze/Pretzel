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
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/statvfs.h>
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

    LOG_TRACE("Mgmtd HTTP request method={} target={}", req.method_string(), target);

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

    if (target == "/api/settings/commit" && req.method() == http::verb::post)
    {
        if (!isAuthenticated(req))
        {
            return unauthorized(req.version(), req.keep_alive());
        }
        return handleSettingsCommit(req);
    }

    if (target == "/api/settings/reload-status" && req.method() == http::verb::get)
    {
        if (!isAuthenticated(req))
        {
            return unauthorized(req.version(), req.keep_alive());
        }
        return handleReloadStatus(req);
    }

    if (target == "/api/devices" && req.method() == http::verb::get)
    {
        if (!isAuthenticated(req))
            return unauthorized(req.version(), req.keep_alive());
        return handleDevices(req);
    }

    if (target.rfind("/api/logs", 0) == 0 && req.method() == http::verb::get)
    {
        if (!isAuthenticated(req))
            return unauthorized(req.version(), req.keep_alive());
        return handleLogs(req);
    }

    if (target == "/api/node-metrics" && req.method() == http::verb::get)
    {
        if (!isAuthenticated(req))
            return unauthorized(req.version(), req.keep_alive());
        return handleNodeMetrics(req);
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

constexpr const char* kSettingsDaemons[] = {
    "engined", "authd", "icmpd", "snmpd", "topologyd",
};

// Tuning domains that are internal infrastructure — not user-configurable
// at runtime and not shown in the Settings UI.
constexpr const char* kHiddenDomains[] = {
    "ipc_connect",
};

} // namespace

HttpRouter::Response HttpRouter::handleSettingsGet(const Request& req)
{
    json daemons = json::object();

    for (const auto* name : kSettingsDaemons)
    {
        const auto& root    = pz::config::Config::daemonConfig(name);
        const auto& allTuning = root.value("tuning", json::object());

        json tuning = json::object();
        for (const auto& [domain, values] : allTuning.items())
        {
            const bool hidden = std::any_of(
                std::begin(kHiddenDomains), std::end(kHiddenDomains),
                [&](const char* d) { return domain == d; });
            if (!hidden)
                tuning[domain] = values;
        }

        daemons[name] = std::move(tuning);
    }

    json body;
    body["daemons"] = std::move(daemons);

    return makeResponse(http::status::ok,
                        body.dump(),
                        "application/json; charset=utf-8",
                        req.version(),
                        req.keep_alive());
}

HttpRouter::Response HttpRouter::handleSettingsCommit(const Request& req)
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

    // Body: { "changes": [ {daemon, domain, values}, ... ] }
    if (!input.contains("changes") || !input["changes"].is_array())
    {
        return badRequest("expected {changes: [{daemon, domain, values}]}");
    }

    const json& changes = input["changes"];

    json results = json::array();
    int applied  = 0;
    int failed   = 0;

    for (const auto& change : changes)
    {
        if (!change.contains("daemon") || !change.contains("domain") || !change.contains("values"))
        {
            results.push_back({{"status", "error"}, {"error", "missing daemon/domain/values"}});
            failed++;
            continue;
        }

        const std::string daemon = change.value("daemon", "");
        const std::string domain = change.value("domain", "");
        const json& values = change["values"];

        if (!values.is_object())
        {
            results.push_back({{"daemon", daemon}, {"domain", domain},
                               {"status", "error"}, {"error", "values must be an object"}});
            failed++;
            continue;
        }

        const bool knownDaemon = std::any_of(
            std::begin(kSettingsDaemons), std::end(kSettingsDaemons),
            [&](const char* d) { return daemon == d; });

        if (!knownDaemon)
        {
            results.push_back({{"daemon", daemon}, {"domain", domain},
                               {"status", "error"}, {"error", "unknown daemon"}});
            failed++;
            continue;
        }

        if (!pz::config::Config::updateTuning(daemon, domain, values))
        {
            results.push_back({{"daemon", daemon}, {"domain", domain},
                               {"status", "error"}, {"error", "failed to persist"}});
            failed++;
            continue;
        }

        LOG_INFO("Mgmtd settings committed daemon={} domain={} keys={}", daemon, domain, values.size());
        results.push_back({{"daemon", daemon}, {"domain", domain}, {"status", "ok"}});
        applied++;
    }

    // Delegate the service-layer restart to engined: a single ConfigReloadRequest
    // unicast is enough — engined fans it out to authd/icmpd/snmpd/topologyd and
    // then restarts itself, ensuring a clean coordinated re-bootstrap.
    if (applied > 0 && m_serviceManager)
    {
        auto msg = std::make_unique<pz::ipc::IpcMessage>();
        msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
        msg->setDst(pz::ipc::IpcDaemon::Engined);
        msg->setCmd(pz::ipc::IpcCmd::ConfigReloadRequest);
        msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));

        m_serviceManager->txRouter().handleIpcMessage(std::move(msg));
        m_serviceManager->startReload();
        LOG_INFO("Mgmtd ConfigReloadRequest sent to engined ({} change(s) applied)", applied);
    }

    const http::status status = (failed == 0)
        ? http::status::ok
        : (applied > 0 ? http::status::ok : http::status::internal_server_error);

    json body;
    body["applied"]   = applied;
    body["failed"]    = failed;
    body["results"]   = results;
    body["reloading"] = (applied > 0);

    return makeResponse(status,
                        body.dump(),
                        "application/json; charset=utf-8",
                        req.version(),
                        req.keep_alive());
}

HttpRouter::Response HttpRouter::handleReloadStatus(const Request& req)
{
    json body;

    if (!m_serviceManager)
    {
        body["status"] = "idle";
        body["elapsed_ms"] = 0;
        return makeResponse(http::status::ok, body.dump(),
                            "application/json; charset=utf-8",
                            req.version(), req.keep_alive());
    }

    const auto s = m_serviceManager->reloadStatus();

    if (s == MgmtdServiceManager::ReloadStatus::Reloading)
        body["status"] = "reloading";
    else if (s == MgmtdServiceManager::ReloadStatus::Complete)
        body["status"] = "complete";
    else
        body["status"] = "idle";

    body["elapsed_ms"] = m_serviceManager->reloadElapsedMs();

    return makeResponse(http::status::ok, body.dump(),
                        "application/json; charset=utf-8",
                        req.version(), req.keep_alive());
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

    // Strip query string before cache lookup (?tab=icmp, etc.)
    std::string staticPath(req.target());
    const auto qpos = staticPath.find('?');
    if (qpos != std::string::npos)
        staticPath.erase(qpos);

    auto file = m_cache->get(staticPath);
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

// ── /api/devices ─────────────────────────────────────────────────────────────

HttpRouter::Response HttpRouter::handleDevices(const Request& req)
{
    json body;
    json devices = json::array();

    if (m_serviceManager)
    {
        const auto ips = m_serviceManager->aliveIps();
        for (const auto& ip : ips)
        {
            devices.push_back({
                {"ip",          ip},
                {"hostname",    ""},
                {"device_name", ""},
                {"vendor",      ""},
                {"status",      "alive"},
            });
        }
    }

    body["devices"] = std::move(devices);
    return makeResponse(http::status::ok, body.dump(),
                        "application/json; charset=utf-8",
                        req.version(), req.keep_alive());
}

// ── /api/logs ─────────────────────────────────────────────────────────────────

namespace
{
// Parse query string key=value pairs from a target like /api/logs?daemon=icmpd&lines=100
std::string queryParam(const std::string& target, const std::string& key)
{
    const auto q = target.find('?');
    if (q == std::string::npos) return {};
    std::string qs = target.substr(q + 1);
    std::istringstream ss(qs);
    std::string token;
    while (std::getline(ss, token, '&'))
    {
        const auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        if (token.substr(0, eq) == key)
            return token.substr(eq + 1);
    }
    return {};
}

// Read last N lines of a file efficiently
std::vector<std::string> tailFile(const std::string& path, int maxLines)
{
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::deque<std::string> lines;
    std::string line;
    while (std::getline(f, line))
    {
        lines.push_back(line);
        if (static_cast<int>(lines.size()) > maxLines)
            lines.pop_front();
    }
    return {lines.begin(), lines.end()};
}

const std::string kLogDir = "/var/log/pretzel";
const std::vector<std::string> kKnownDaemons = {
    "ipcd", "engined", "mgmtd", "authd", "icmpd", "snmpd", "topologyd"
};
} // namespace

HttpRouter::Response HttpRouter::handleLogs(const Request& req)
{
    const std::string target(req.target());
    std::string daemon  = queryParam(target, "daemon");
    const int   maxLines = [&]{
        const auto s = queryParam(target, "lines");
        return s.empty() ? 200 : std::max(1, std::min(2000, std::stoi(s)));
    }();

    // Validate daemon name (prevent path traversal)
    if (!daemon.empty())
    {
        const bool known = std::any_of(kKnownDaemons.begin(), kKnownDaemons.end(),
            [&](const std::string& d) { return d == daemon; });
        if (!known)
        {
            return makeResponse(http::status::bad_request,
                                json{{"error", "unknown daemon"}}.dump(),
                                "application/json; charset=utf-8",
                                req.version(), req.keep_alive());
        }
    }

    json body;

    if (!daemon.empty())
    {
        const std::string path = kLogDir + "/" + daemon + ".log";
        const auto lines = tailFile(path, maxLines);
        json arr = json::array();
        for (const auto& l : lines) arr.push_back(l);
        body["daemon"] = daemon;
        body["lines"]  = std::move(arr);
    }
    else
    {
        // Return last few lines from each known daemon
        json all = json::object();
        for (const auto& d : kKnownDaemons)
        {
            const std::string path = kLogDir + "/" + d + ".log";
            const auto lines = tailFile(path, 50);
            json arr = json::array();
            for (const auto& l : lines) arr.push_back(l);
            all[d] = std::move(arr);
        }
        body["daemons"] = std::move(all);
    }

    return makeResponse(http::status::ok, body.dump(),
                        "application/json; charset=utf-8",
                        req.version(), req.keep_alive());
}

// ── /api/node-metrics ─────────────────────────────────────────────────────────

namespace
{
struct CpuSnapshot { uint64_t total{0}, idle{0}; };

CpuSnapshot readCpuSnapshot()
{
    std::ifstream f("/proc/stat");
    std::string token;
    CpuSnapshot snap;
    if (!f.is_open()) return snap;
    f >> token; // "cpu"
    std::vector<uint64_t> vals;
    uint64_t v;
    while (vals.size() < 10 && f >> v)
        vals.push_back(v);
    if (vals.size() >= 4)
    {
        snap.idle  = vals[3] + (vals.size() > 4 ? vals[4] : 0); // idle + iowait
        snap.total = 0;
        for (auto x : vals) snap.total += x;
    }
    return snap;
}

double readMemPct()
{
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) return 0.0;
    uint64_t total = 0, avail = 0;
    std::string key;
    uint64_t val;
    std::string unit;
    while (f >> key >> val)
    {
        f >> unit; // "kB"
        if (key == "MemTotal:")     total = val;
        if (key == "MemAvailable:") avail = val;
        if (total && avail) break;
    }
    return total ? 100.0 * (total - avail) / total : 0.0;
}

double readDiskPct(const char* path = "/")
{
    struct statvfs st {};
    if (::statvfs(path, &st) != 0) return 0.0;
    const uint64_t total = static_cast<uint64_t>(st.f_blocks) * st.f_frsize;
    const uint64_t avail = static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
    return total ? 100.0 * (total - avail) / total : 0.0;
}

// Simple moving-average CPU tracker
std::mutex s_cpuMtx;
CpuSnapshot s_cpuPrev;
double      s_cpuPct{0.0};
} // namespace

HttpRouter::Response HttpRouter::handleNodeMetrics(const Request& req)
{
    const auto cur = readCpuSnapshot();
    {
        std::lock_guard<std::mutex> lk(s_cpuMtx);
        if (s_cpuPrev.total > 0 && cur.total > s_cpuPrev.total)
        {
            const uint64_t dt = cur.total - s_cpuPrev.total;
            const uint64_t di = cur.idle  - s_cpuPrev.idle;
            s_cpuPct = 100.0 * (dt - di) / dt;
        }
        s_cpuPrev = cur;
    }

    json body;
    body["cpu_pct"]  = std::round(s_cpuPct  * 10) / 10.0;
    body["mem_pct"]  = std::round(readMemPct()  * 10) / 10.0;
    body["disk_pct"] = std::round(readDiskPct() * 10) / 10.0;

    return makeResponse(http::status::ok, body.dump(),
                        "application/json; charset=utf-8",
                        req.version(), req.keep_alive());
}

} // namespace pz::mgmtd
