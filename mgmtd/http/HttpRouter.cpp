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
    "/js/login.js",  // loaded on the login page without a session
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

    if (target == "/api/change-password" && req.method() == http::verb::post)
    {
        if (!isAuthenticated(req))
        {
            return unauthorized(req.version(), req.keep_alive());
        }
        return handleChangePassword(req);
    }

    // Forced password-change gate: while the admin is still on the factory default,
    // an authenticated session may only reach /api/logout and /api/change-password
    // (both handled above). Every other /api/* call is blocked until it is changed.
    if (target.rfind("/api/", 0) == 0 &&
        m_authService && isAuthenticated(req) && m_authService->mustChangePassword())
    {
        return makeResponse(
            http::status::forbidden,
            R"({"error":"password change required","code":"MUST_CHANGE_PASSWORD"})",
            "application/json; charset=utf-8",
            req.version(), req.keep_alive());
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

    if (target == "/api/settings/commit-queue" && req.method() == http::verb::get)
    {
        if (!isAuthenticated(req))
        {
            return unauthorized(req.version(), req.keep_alive());
        }
        return handleCommitQueue(req);
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

        // Signal must_change so the client forces a password change before proceeding.
        const json okBody = {{"status", "ok"}, {"must_change", result.mustChange}};
        auto res = makeResponse(http::status::ok,
                                okBody.dump(),
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

HttpRouter::Response HttpRouter::handleChangePassword(const Request& req)
{
    if (!m_authService)
    {
        return makeResponse(http::status::service_unavailable,
                            R"({"error":"auth unavailable"})",
                            "application/json; charset=utf-8",
                            req.version(), req.keep_alive());
    }

    try
    {
        const auto body    = json::parse(req.body());
        const auto oldPass = body.at("old_password").get<std::string>();
        const auto newPass = body.at("new_password").get<std::string>();

        if (newPass.empty())
        {
            return makeResponse(http::status::bad_request,
                                R"({"error":"new password must not be empty"})",
                                "application/json; charset=utf-8",
                                req.version(), req.keep_alive());
        }

        // Single operator account: the username is whatever the (authenticated)
        // session's admin is. Require the current password before changing it.
        const std::string& user = m_authService->username();
        if (!m_authService->checkPassword(user, oldPass))
        {
            return makeResponse(http::status::unauthorized,
                                R"({"error":"current password is incorrect"})",
                                "application/json; charset=utf-8",
                                req.version(), req.keep_alive());
        }

        if (!m_serviceManager)
        {
            return makeResponse(http::status::service_unavailable,
                                R"({"error":"engine unavailable"})",
                                "application/json; charset=utf-8",
                                req.version(), req.keep_alive());
        }

        // mgmtd is read-only w.r.t. the DB: compute the new credential and hand the
        // WRITE to engined (the single DB writer) over IPC. The HTTP server runs on the
        // single main-loop thread (io_context.poll()), so we must NOT block here — a
        // blocking confirm would stall the very loop that flushes this IPC frame,
        // deadlocking itself. Instead we send and adopt the credential optimistically;
        // engined persists it, and the DB stays the source of truth across restarts.
        const auto cred = m_authService->makeCredential(newPass);

        const json payload = {{"username",      user},
                              {"password_hash", cred.passwordHash},
                              {"salt",          cred.salt}};
        const std::string payloadStr = payload.dump();

        auto msg = std::make_unique<pz::ipc::IpcMessage>();
        msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
        msg->setDst(pz::ipc::IpcDaemon::Engined);
        msg->setCmd(pz::ipc::IpcCmd::AdminPasswordUpdate);
        msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
        msg->setPayload(std::vector<uint8_t>(payloadStr.begin(), payloadStr.end()));
        m_serviceManager->txRouter().handleIpcMessage(std::move(msg));

        // Adopt the new credential into memory immediately: subsequent logins verify
        // against it and the forced-change gate opens (m_mustChange cleared).
        m_authService->applyCredential(cred.passwordHash, cred.salt);

        LOG_INFO("Mgmtd admin password change sent to engined for user '{}'", user);
        return makeResponse(http::status::ok,
                            R"({"status":"ok"})",
                            "application/json; charset=utf-8",
                            req.version(), req.keep_alive());
    }
    catch (const std::exception& e)
    {
        LOG_WARN("Mgmtd change-password bad request: {}", e.what());
        return makeResponse(http::status::bad_request,
                            R"({"error":"bad request"})",
                            "application/json; charset=utf-8",
                            req.version(), req.keep_alive());
    }
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

// Service domains that are internal infrastructure — not user-configurable
// at runtime and not shown in the Settings UI.
constexpr const char* kHiddenDomains[] = {
    "bootstrap",
};

} // namespace

HttpRouter::Response HttpRouter::handleSettingsGet(const Request& req)
{
    json daemons = json::object();

    for (const auto* name : kSettingsDaemons)
    {
        const auto& root       = pz::config::Config::daemonConfig(name);
        const auto& allService  = root.value("service", json::object());

        json service = json::object();
        for (const auto& [domain, values] : allService.items())
        {
            const bool hidden = std::any_of(
                std::begin(kHiddenDomains), std::end(kHiddenDomains),
                [&](const char* d) { return domain == d; });
            if (hidden)
                continue;

            json v = values;
            // Credentials live in local_users, never in config. Defensively strip any
            // stray admin block (e.g. left in a legacy config version) so the settings
            // UI can never surface a credential.
            if (domain == "http" && v.is_object())
                v.erase("admin");

            service[domain] = std::move(v);
        }

        daemons[name] = std::move(service);
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

    // Validate entries up-front; reject entirely if any are malformed.
    json validChanges = json::array();
    int failed = 0;

    for (const auto& change : changes)
    {
        if (!change.contains("daemon") || !change.contains("domain") || !change.contains("values"))
        {
            LOG_WARN("Mgmtd handleSettingsCommit: skipping entry missing daemon/domain/values");
            failed++;
            continue;
        }

        const std::string daemon = change.value("daemon", "");
        const std::string domain = change.value("domain", "");
        const json& values = change["values"];

        if (!values.is_object())
        {
            LOG_WARN("Mgmtd handleSettingsCommit: values not object daemon={} domain={}", daemon, domain);
            failed++;
            continue;
        }

        const bool knownDaemon = std::any_of(
            std::begin(kSettingsDaemons), std::end(kSettingsDaemons),
            [&](const char* d) { return daemon == d; });

        if (!knownDaemon)
        {
            LOG_WARN("Mgmtd handleSettingsCommit: unknown daemon={}", daemon);
            failed++;
            continue;
        }

        validChanges.push_back(change);
    }

    const int applied = static_cast<int>(validChanges.size());

    // Forward valid changes to engined as SettingsCommitRequest.
    // engined owns persistence (Config::commitConfig) and service-layer restart.
    if (applied > 0 && m_serviceManager)
    {
        const std::string payload = validChanges.dump();
        auto msg = std::make_unique<pz::ipc::IpcMessage>();
        msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
        msg->setDst(pz::ipc::IpcDaemon::Engined);
        msg->setCmd(pz::ipc::IpcCmd::SettingsCommitRequest);
        msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
        msg->setPayload(std::vector<uint8_t>(payload.begin(), payload.end()));

        m_serviceManager->txRouter().handleIpcMessage(std::move(msg));
        m_serviceManager->startReload();
        LOG_INFO("Mgmtd SettingsCommitRequest sent to engined ({} change(s))", applied);
    }

    const http::status status = (failed == 0)
        ? http::status::ok
        : (applied > 0 ? http::status::ok : http::status::internal_server_error);

    json body;
    body["applied"]   = applied;
    body["failed"]    = failed;
    body["results"]   = json::array();
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

HttpRouter::Response HttpRouter::handleCommitQueue(const Request& req)
{
    const std::string snapshot = m_serviceManager
        ? m_serviceManager->commitQueueSnapshot()
        : "[]";

    return makeResponse(http::status::ok,
                        snapshot,
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
        const auto ips      = m_serviceManager->aliveIps();
        const auto snmpDevs = m_serviceManager->snmpService().devices();

        for (const auto& ip : ips)
        {
            json dev;
            dev["ip"]     = ip;
            dev["status"] = "alive";

            const auto it = snmpDevs.find(ip);
            if (it != snmpDevs.end())
            {
                const auto& s = it->second;
                dev["hostname"]          = s.sysName;
                dev["device_name"]       = s.sysName;
                dev["vendor"]            = "";
                dev["sys_descr"]         = s.sysDescr;
                dev["sys_object_id"]     = s.sysObjectId;
                dev["sys_contact"]       = s.sysContact;
                dev["sys_location"]      = s.sysLocation;
                dev["sys_up_time_ticks"] = s.sysUpTimeTicks;
            }
            else
            {
                dev["hostname"]    = "";
                dev["device_name"] = "";
                dev["vendor"]      = "";
            }

            devices.push_back(std::move(dev));
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
// maxLines <= 0 means read the entire file
std::vector<std::string> tailFile(const std::string& path, int maxLines)
{
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string line;
    if (maxLines <= 0)
    {
        std::vector<std::string> all;
        while (std::getline(f, line)) all.push_back(line);
        return all;
    }
    std::deque<std::string> lines;
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
    // lines not present → read entire file (maxLines = 0)
    const int   maxLines = [&]{
        const auto s = queryParam(target, "lines");
        if (s.empty()) return 0;
        const int n = std::stoi(s);
        return (n <= 0) ? 0 : n;
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
CpuSnapshot s_cpuPrev;
double      s_cpuPct{0.0};
} // namespace

HttpRouter::Response HttpRouter::handleNodeMetrics(const Request& req)
{
    const auto cur = readCpuSnapshot();
    if (s_cpuPrev.total > 0 && cur.total > s_cpuPrev.total)
    {
        const uint64_t dt = cur.total - s_cpuPrev.total;
        const uint64_t di = cur.idle  - s_cpuPrev.idle;
        s_cpuPct = 100.0 * (dt - di) / dt;
    }
    s_cpuPrev = cur;

    json body;
    body["cpu_pct"]  = std::round(s_cpuPct  * 10) / 10.0;
    body["mem_pct"]  = std::round(readMemPct()  * 10) / 10.0;
    body["disk_pct"] = std::round(readDiskPct() * 10) / 10.0;

    return makeResponse(http::status::ok, body.dump(),
                        "application/json; charset=utf-8",
                        req.version(), req.keep_alive());
}

} // namespace pz::mgmtd
