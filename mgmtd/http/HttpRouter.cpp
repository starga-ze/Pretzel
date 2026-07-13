#include "http/HttpRouter.h"

#include "http/HttpCache.h"
#include "service/auth/AuthService.h"
#include "service/metrics/MetricService.h"
#include "service/MgmtdServiceManager.h"
#include "router/MgmtdTxRouter.h"

#include "config/Config.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <sys/statvfs.h>
#include <utility>
#include <vector>

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

    LOG_TRACE("HTTP request (method={}, target={})", req.method_string(), target);

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

    // SSO (Okta via authd) — all public: they run before any session exists.
    if (target == "/api/auth/sso/info" && req.method() == http::verb::get)
    {
        return handleSsoInfo(req);
    }
    if (target == "/api/auth/sso/login" && req.method() == http::verb::get)
    {
        return handleSsoLogin(req);
    }
    if (target == "/api/auth/saml/acs" && req.method() == http::verb::post)
    {
        return handleSamlAcs(req);
    }
    if (target.rfind("/api/auth/saml/result", 0) == 0 && req.method() == http::verb::get)
    {
        return handleSamlResult(req);
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

    if (target == "/api/whoami" && req.method() == http::verb::get)
    {
        if (!isAuthenticated(req))
        {
            return unauthorized(req.version(), req.keep_alive());
        }
        return handleWhoami(req);
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

    // Laboratory: client POSTs a canvas-rasterized PDF; we reflect it back as a real
    // server-served attachment so the download URL is this route (not blob:/data:).
    if (target == "/api/lab/cidi-export" && req.method() == http::verb::post)
    {
        if (!isAuthenticated(req))
            return unauthorized(req.version(), req.keep_alive());
        return handleLabExport(req);
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
        LOG_WARN("login bad request (error={})", e.what());
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

        LOG_INFO("admin password change sent to engined (user={})", user);
        return makeResponse(http::status::ok,
                            R"({"status":"ok"})",
                            "application/json; charset=utf-8",
                            req.version(), req.keep_alive());
    }
    catch (const std::exception& e)
    {
        LOG_WARN("change-password bad request (error={})", e.what());
        return makeResponse(http::status::bad_request,
                            R"({"error":"bad request"})",
                            "application/json; charset=utf-8",
                            req.version(), req.keep_alive());
    }
}

HttpRouter::Response HttpRouter::handleWhoami(const Request& req)
{
    // Identify the signed-in user for the UI. Prefer the per-session identity (the SSO
    // subject/email, or the local admin name); fall back to the local operator name for
    // legacy sessions minted before the session carried a username.
    std::string user;
    if (m_authService)
    {
        user = m_authService->sessionUser(extractSession(req));
        if (user.empty())
            user = m_authService->username();
    }

    const json out = {{"username", user}};
    return makeResponse(http::status::ok, out.dump(),
                        "application/json; charset=utf-8",
                        req.version(), req.keep_alive());
}

HttpRouter::Response HttpRouter::handleStatus(const Request& req)
{
    json body;

    body["uptime_seconds"] = 0.0;

    json daemons = json::array();

    if (m_serviceManager)
    {
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
    "engined", "authd", "icmpd", "scand", "topologyd",
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
            LOG_WARN("skipping settings-commit entry missing daemon/domain/values");
            failed++;
            continue;
        }

        const std::string daemon = change.value("daemon", "");
        const std::string domain = change.value("domain", "");
        const json& values = change["values"];

        if (!values.is_object())
        {
            LOG_WARN("settings-commit values not an object (daemon={}, domain={})", daemon, domain);
            failed++;
            continue;
        }

        const bool knownDaemon = std::any_of(
            std::begin(kSettingsDaemons), std::end(kSettingsDaemons),
            [&](const char* d) { return daemon == d; });

        if (!knownDaemon)
        {
            LOG_WARN("settings-commit unknown daemon (daemon={})", daemon);
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
        LOG_INFO("SettingsCommitRequest sent to engined (changes={})", applied);
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
    int networkCount = 0;
    int serverCount  = 0;
    int unknownCount = 0;

    if (m_serviceManager)
    {
        const auto groups = m_serviceManager->deviceService().groups();

        // Rack allocation lives in running-config (engined.service.rack.racks); each
        // allocated device carries a literal {device, ip, status}. Build ip -> rack-name
        // so the Devices view can show which rack a device is declared in (replacing SNMP
        // sysLocation). The green/red reconciliation against probe state is done client-side.
        std::unordered_map<std::string, std::string> rackByIp;
        {
            const auto& eng  = pz::config::Config::daemonConfig("engined");
            const auto  svc  = eng.value("service", json::object());
            const auto  rack = svc.value("rack", json::object());
            for (const auto& rk : rack.value("racks", json::array()))
            {
                const std::string name = rk.value("name", "");
                for (const auto& d : rk.value("devices", json::array()))
                    rackByIp[d.value("ip", "")] = name;
            }
        }

        for (const auto& g : groups)
        {
            if (g.type == "network")      ++networkCount;
            else if (g.type == "server")  ++serverCount;
            else                          ++unknownCount;

            // location = name of the rack this device's IP is allocated to (if any).
            std::string location;
            for (const auto& ip : g.ips)
            {
                auto it = rackByIp.find(ip);
                if (it != rackByIp.end()) { location = it->second; break; }
            }

            json ifaces = json::array();
            for (const auto& itf : g.interfaces)
            {
                ifaces.push_back({
                    {"ip",       itf.ip},
                    {"netmask",  itf.netmask},
                    {"if_index", itf.ifIndex},
                    {"if_name",  itf.ifName},
                });
            }

            devices.push_back({
                {"primary_ip",        g.primaryIp},
                {"ips",               g.ips},
                {"type",              g.type},
                {"subtype",           g.subtype},
                {"vendor",            g.vendor},
                {"hostname",          g.hostname},
                {"sys_descr",         g.sysDescr},
                {"sys_object_id",     g.sysObjectId},
                {"sys_contact",       g.sysContact},
                {"location",          location},
                {"sys_up_time_ticks", g.sysUpTimeTicks},
                {"interface_macs",    g.interfaceMacs},
                {"interfaces",        std::move(ifaces)},
                {"if_table",          g.ifTable},
                {"lldp_neighbors",    g.lldpNeighbors},
                {"host_mac",          g.hostMac},
                {"has_snmp",          g.hasSnmp},
            });
        }
    }

    body["summary"] = {
        {"total",   networkCount + serverCount + unknownCount},
        {"network", networkCount},
        {"server",  serverCount},
        {"unknown", unknownCount},
    };
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
    "ipcd", "engined", "mgmtd", "authd", "icmpd", "scand", "topologyd"
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

// ── SSO (Okta via authd) helpers ────────────────────────────────────────────
namespace
{

std::string ssoRandomHex(std::size_t nBytes)
{
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string s;
    s.reserve(nBytes * 2);
    for (std::size_t i = 0; i < nBytes * 2; ++i) s.push_back(hex[dist(gen)]);
    return s;
}

std::string ssoUtcNow()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string ssoBase64(const std::string& in)
{
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    const auto* d = reinterpret_cast<const unsigned char*>(in.data());
    std::size_t len = in.size(), i = 0;
    for (; i + 2 < len; i += 3)
    {
        std::uint32_t n = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        out.push_back(tbl[(n >> 18) & 63]); out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);  out.push_back(tbl[n & 63]);
    }
    if (i < len)
    {
        std::uint32_t n = d[i] << 16;
        if (i + 1 < len) n |= d[i + 1] << 8;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back((i + 1 < len) ? tbl[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

// application/x-www-form-urlencoded decode ('+' → space, %XX → byte).
std::string ssoUrlDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (c == '+') { out.push_back(' '); }
        else if (c == '%' && i + 2 < s.size())
        {
            auto hexv = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            int hi = hexv(s[i + 1]), lo = hexv(s[i + 2]);
            if (hi >= 0 && lo >= 0) { out.push_back(static_cast<char>((hi << 4) | lo)); i += 2; }
            else out.push_back(c);
        }
        else out.push_back(c);
    }
    return out;
}

// Extract and URL-decode a field from an x-www-form-urlencoded body.
std::string ssoFormField(const std::string& body, const std::string& key)
{
    const std::string pfx = key + "=";
    std::size_t pos = 0;
    while (pos < body.size())
    {
        std::size_t amp = body.find('&', pos);
        std::string pair = body.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (pair.rfind(pfx, 0) == 0) return ssoUrlDecode(pair.substr(pfx.size()));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

std::string ssoHtmlAttr(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '&': out += "&amp;";  break;
        case '"': out += "&quot;"; break;
        case '<': out += "&lt;";   break;
        case '>': out += "&gt;";   break;
        default:  out.push_back(c);
        }
    }
    return out;
}

// The service.auth block from authd's running config (method + oidc/saml settings).
nlohmann::json ssoAuthConfig()
{
    const auto& root = pz::config::Config::daemonConfig("authd");
    return root.value("service", nlohmann::json::object())
               .value("auth", nlohmann::json::object());
}

// Standard base64 decode (skips non-alphabet chars such as whitespace; stops at '=').
std::string labBase64Decode(const std::string& in)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    out.reserve(in.size() * 3 / 4);
    int buf = 0, bits = 0;
    for (char c : in)
    {
        if (c == '=') break;
        const int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace

HttpRouter::Response HttpRouter::handleSsoInfo(const Request& req)
{
    const auto auth = ssoAuthConfig();
    const std::string method = auth.value("method", std::string("oidc"));
    bool enabled = false;
    if (method == "saml")
        enabled = auth.value("saml", json::object()).value("enabled", false);
    else
        enabled = auth.value("oidc", json::object()).value("enabled", false);

    // Gate federated login behind admin provisioning. While the local admin is still on
    // the factory-default password (must_change), the forced password-change gate 403s
    // every /api/* call — so an SSO session would authenticate only to hit that wall.
    // Report SSO as disabled until an admin account has been set up; the login page
    // hides the "Sign in with Okta" button on {enabled:false}.
    const bool adminSetup = !(m_authService && m_authService->mustChangePassword());
    enabled = enabled && adminSetup;

    const json out = {{"enabled", enabled},
                      {"method", method},
                      {"label", "Sign in with Okta"},
                      {"admin_setup_required", !adminSetup}};
    return makeResponse(http::status::ok, out.dump(),
                        "application/json; charset=utf-8",
                        req.version(), req.keep_alive());
}

HttpRouter::Response HttpRouter::handleSsoLogin(const Request& req)
{
    auto redirectErr = [&](const std::string& code) {
        auto r = makeResponse(http::status::found, "", "text/plain",
                              req.version(), req.keep_alive());
        r.set(http::field::location, "/index.html?sso_error=" + code);
        return r;
    };

    // Defense in depth for the SSO gate: even if a stale page still shows the button,
    // refuse to start the IdP round-trip while the local admin is unprovisioned — the
    // resulting session would only 403 on every /api/*. Mirrors handleSsoInfo.
    if (m_authService && m_authService->mustChangePassword())
        return redirectErr("setup_required");

    const auto auth = ssoAuthConfig();
    if (auth.value("method", std::string("oidc")) != "saml")
        return redirectErr("not_configured");

    const auto saml = auth.value("saml", json::object());
    if (!saml.value("enabled", false))
        return redirectErr("disabled");

    const std::string idp = saml.value("idp_sso_url", "");
    const std::string sp  = saml.value("sp_entity_id", "");
    const std::string acs = saml.value("acs_url", "");
    if (idp.empty() || acs.empty())
        return redirectErr("misconfigured");

    // SP-initiated AuthnRequest over the HTTP-POST binding (base64, no DEFLATE). It is
    // unsigned (public parameters), so mgmtd builds it directly; authd remains the sole
    // verifier of the returned SAMLResponse.
    const std::string id  = "_" + ssoRandomHex(16);
    const std::string xml =
        "<samlp:AuthnRequest xmlns:samlp=\"urn:oasis:names:tc:SAML:2.0:protocol\""
        " xmlns:saml=\"urn:oasis:names:tc:SAML:2.0:assertion\""
        " ID=\"" + id + "\" Version=\"2.0\" IssueInstant=\"" + ssoUtcNow() + "\""
        " Destination=\"" + idp + "\""
        " ProtocolBinding=\"urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST\""
        " AssertionConsumerServiceURL=\"" + acs + "\">"
        "<saml:Issuer>" + sp + "</saml:Issuer></samlp:AuthnRequest>";

    const std::string html =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Redirecting…</title></head>"
        "<body onload=\"document.forms[0].submit()\">"
        "<form method=\"POST\" action=\"" + ssoHtmlAttr(idp) + "\">"
        "<input type=\"hidden\" name=\"SAMLRequest\" value=\"" + ssoHtmlAttr(ssoBase64(xml)) + "\"/>"
        "<noscript><button type=\"submit\">Continue to Okta</button></noscript>"
        "</form></body></html>";

    return makeResponse(http::status::ok, html, "text/html; charset=utf-8",
                        req.version(), req.keep_alive());
}

HttpRouter::Response HttpRouter::handleSamlAcs(const Request& req)
{
    auto redirectErr = [&](const std::string& code) {
        auto r = makeResponse(http::status::found, "", "text/plain",
                              req.version(), req.keep_alive());
        r.set(http::field::location, "/index.html?sso_error=" + code);
        return r;
    };

    const std::string samlResponse = ssoFormField(req.body(), "SAMLResponse");
    if (samlResponse.empty())   return redirectErr("no_saml_response");
    if (!m_serviceManager)      return redirectErr("unavailable");

    // Hand the SAMLResponse to authd for verification (fire-and-forget). The verdict
    // returns asynchronously via MgmtdRxRouter, keyed by this ticket (= IPC seqNo).
    const std::uint32_t ticket = m_ssoTicket++;
    const json payload = {{"saml_response", samlResponse}};
    const std::string ps = payload.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Authd);
    msg->setCmd(pz::ipc::IpcCmd::AuthSamlAcsRequest);
    msg->setSeqNo(ticket);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(ps.begin(), ps.end()));
    m_serviceManager->txRouter().handleIpcMessage(std::move(msg));

    // "Signing in…" page polls for the verdict; on success the poll response sets the
    // session cookie and redirects to the dashboard.
    const std::string t = std::to_string(ticket);
    const std::string html =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Signing in…</title></head>"
        "<body><p style=\"font-family:sans-serif\">Signing in…</p><script>"
        "(function(){var t=" + t + ",n=0;"
        "function go(){n++;if(n>40){location.href='/index.html?sso_error=timeout';return;}"
        "fetch('/api/auth/saml/result?ticket='+t).then(function(r){return r.json();})"
        ".then(function(d){"
        "if(d.status==='ok'){location.href=d.redirect||'/dashboard.html';}"
        "else if(d.status==='error'){location.href='/index.html?sso_error='+encodeURIComponent(d.error||'failed');}"
        "else{setTimeout(go,600);}"
        "}).catch(function(){setTimeout(go,900);});}"
        "go();})();"
        "</script></body></html>";

    return makeResponse(http::status::ok, html, "text/html; charset=utf-8",
                        req.version(), req.keep_alive());
}

HttpRouter::Response HttpRouter::handleSamlResult(const Request& req)
{
    auto jsonResp = [&](const json& j) {
        return makeResponse(http::status::ok, j.dump(),
                            "application/json; charset=utf-8",
                            req.version(), req.keep_alive());
    };

    const std::string target(req.target());
    std::uint32_t ticket = 0;
    if (auto pos = target.find("ticket="); pos != std::string::npos)
        ticket = static_cast<std::uint32_t>(std::strtoul(target.c_str() + pos + 7, nullptr, 10));

    if (!m_serviceManager || ticket == 0)
        return jsonResp({{"status", "error"}, {"error", "bad ticket"}});

    auto result = m_serviceManager->takeSsoResult(ticket);
    if (!result)
        return jsonResp({{"status", "pending"}});

    try
    {
        const auto verdict = json::parse(*result);
        if (verdict.value("success", false))
        {
            const std::string user = verdict.value("username", "");
            const std::string sid  = m_authService
                                    ? m_authService->createSsoSession(user)
                                    : std::string();
            if (sid.empty())
                return jsonResp({{"status", "error"}, {"error", "session error"}});

            auto res = makeResponse(http::status::ok,
                                    json({{"status", "ok"}, {"redirect", "/dashboard.html"}}).dump(),
                                    "application/json; charset=utf-8",
                                    req.version(), req.keep_alive());
            res.set(http::field::set_cookie,
                    "session=" + sid + "; HttpOnly; Path=/; SameSite=Strict");
            LOG_INFO("sso login ok (user={})", user);
            return res;
        }
        return jsonResp({{"status", "error"},
                         {"error", verdict.value("error", std::string("verification failed"))}});
    }
    catch (const std::exception& e)
    {
        LOG_WARN("sso result parse error: {}", e.what());
        return jsonResp({{"status", "error"}, {"error", "bad result"}});
    }
}

// ── /api/lab/cidi-export ──────────────────────────────────────────────────────
// The browser rasterizes the records on a <canvas>, wraps that image in a PDF, and
// POSTs it here (base64, form field "pdf"). We decode and stream it straight back as
// an attachment. Net effect: the download is served from THIS real server route — the
// event's File download URL is https://<host>/api/lab/cidi-export, not a blob:/data:
// URL — while the file's content stays canvas pixels (no selectable text).
HttpRouter::Response HttpRouter::handleLabExport(const Request& req)
{
    std::string pdf = labBase64Decode(ssoFormField(req.body(), "pdf"));
    if (pdf.empty())
    {
        return makeResponse(http::status::bad_request,
                            R"({"error":"empty pdf"})",
                            "application/json; charset=utf-8",
                            req.version(), req.keep_alive());
    }

    std::string filename = ssoFormField(req.body(), "name");
    filename.erase(std::remove_if(filename.begin(), filename.end(),
        [](char c) { return c == '"' || c == '\\' || c == '/' || c == '\r' || c == '\n'; }),
        filename.end());
    if (filename.empty()) filename = "cidi-export.pdf";

    const std::size_t bytes = pdf.size();

    Response res{http::status::ok, req.version()};
    res.set(http::field::server,              "pz-mgmtd");
    res.set(http::field::content_type,        "application/pdf");
    res.set(http::field::content_disposition, "attachment; filename=\"" + filename + "\"");
    res.keep_alive(req.keep_alive());
    res.body() = std::move(pdf);
    res.prepare_payload();

    LOG_INFO("lab cidi-export served (name={}, bytes={})", filename, bytes);
    return res;
}

} // namespace pz::mgmtd
