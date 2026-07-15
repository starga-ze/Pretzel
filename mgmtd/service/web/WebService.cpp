#include "service/web/WebService.h"

#include "service/web/WebEvent.h"
#include "service/web/WebAction.h"
#include "service/MgmtdServiceManager.h"

#include "router/MgmtdTxRouter.h"

#include "config/Config.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <deque>
#include <fstream>
#include <memory>
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

namespace
{

// Static pages that do NOT require a session. Everything else under "/" is protected.
constexpr const char* kPublicPages[] = {
    "/",
    "/index.html",
    "/css/main.css",
    "/js/main.js",
    "/js/login.js",  // loaded on the login page without a session
};

// Fill a response slot in one shot; content type defaults to JSON.
void fill(pz::http::HttpResponse& r,
          int                     status,
          std::string             body,
          std::string             contentType = "application/json; charset=utf-8")
{
    r.status      = status;
    r.contentType = std::move(contentType);
    r.body        = std::move(body);
}

// Shorthand for 401 Unauthorized.
void unauthorized(pz::http::HttpResponse& r)
{
    fill(r, 401, R"({"error":"unauthorized"})");
}

} // namespace

void WebService::setCache(std::shared_ptr<pz::http::StaticFileCache> cache)
{
    m_cache = std::move(cache);
}

void WebService::handleEvent(MgmtdServiceManager& sm, const WebEvent& event)
{
    Response resp;  // default 404 for unmatched routes
    route(sm, event.request(), resp);

    // Event -> Action: post a WebAction carrying the response + the SessionId. Egress happens
    // later, in handleAction (the action drain), never inline here — the same shape as IPC.
    sm.postAction(std::make_unique<WebAction>(std::move(resp), event.sessionId()));
}

void WebService::handleAction(MgmtdServiceManager& sm, WebAction& action)
{
    LOG_DEBUG("WebService handleAction (domain={})", static_cast<std::uint32_t>(action.domain()));
    // Egress via the TxRouter — the analogue of BootstrapService::handleAction sending an IPC
    // message. The response body is moved (not copied) since static-asset bodies can be large.
    sm.txRouter().handleHttpMessage(std::move(action.response()), action.sessionId());
}

void WebService::route(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    const std::string& target = req.target;

    LOG_TRACE("HTTP request (method={}, target={})", req.method, target);

    // ── Public API routes (no auth required) ──────────────────────────────
    if (target == "/metrics" && req.method == "GET")
        return handleMetric(sm, req, resp);

    if (target == "/health" && req.method == "GET")
        return handleHealth(sm, req, resp);

    if (target == "/api/login" && req.method == "POST")
        return handleLogin(sm, req, resp);

    // SSO (Okta via authd) — all public: they run before any session exists.
    if (target == "/api/auth/sso/info" && req.method == "GET")
        return handleSsoInfo(sm, req, resp);
    if (target == "/api/auth/sso/login" && req.method == "GET")
        return handleSsoLogin(sm, req, resp);
    if (target == "/api/auth/saml/acs" && req.method == "POST")
        return handleSamlAcs(sm, req, resp);
    if (target.rfind("/api/auth/saml/result", 0) == 0 && req.method == "GET")
        return handleSamlResult(sm, req, resp);

    // ── Auth-required API routes ──────────────────────────────────────────
    if (target == "/api/logout" && req.method == "POST")
    {
        // logout is safe to call even without a valid session
        return handleLogout(sm, req, resp);
    }

    if (target == "/api/change-password" && req.method == "POST")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleChangePassword(sm, req, resp);
    }

    // Forced password-change gate: while the admin is still on the factory default,
    // an authenticated session may only reach /api/logout and /api/change-password
    // (both handled above). Every other /api/* call is blocked until it is changed.
    if (target.rfind("/api/", 0) == 0 &&
        isAuthenticated(sm, req) && sm.authService().mustChangePassword())
    {
        return fill(resp, 403,
                    R"({"error":"password change required","code":"MUST_CHANGE_PASSWORD"})");
    }

    if (target == "/api/whoami" && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleWhoami(sm, req, resp);
    }

    if (target == "/api/status" && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleStatus(sm, req, resp);
    }

    if (target == "/api/settings" && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleSettingsGet(sm, req, resp);
    }

    if (target == "/api/settings/commit" && req.method == "POST")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleSettingsCommit(sm, req, resp);
    }

    if (target == "/api/settings/reload-status" && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleReloadStatus(sm, req, resp);
    }

    if (target == "/api/settings/commit-queue" && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleCommitQueue(sm, req, resp);
    }

    if (target == "/api/devices" && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleDevices(sm, req, resp);
    }

    if (target.rfind("/api/logs", 0) == 0 && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleLogs(sm, req, resp);
    }

    if (target == "/api/node-metrics" && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleNodeMetrics(sm, req, resp);
    }

    // Laboratory: client POSTs a canvas-rasterized PDF; we reflect it back as a real
    // server-served attachment so the download URL is this route (not blob:/data:).
    if (target == "/api/lab/cidi-export" && req.method == "POST")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleLabExport(sm, req, resp);
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

        if (!isPublic && !isAuthenticated(sm, req))
        {
            // Redirect browsers to login page instead of a bare 401.
            resp.status      = 302;
            resp.contentType = "text/plain; charset=utf-8";
            resp.body.clear();
            resp.location    = "/index.html";
            return;
        }

        return handleStatic(sm, req, resp);
    }

    // Unmatched: leave the default 404 the response was seeded with.
}

// ── Handlers ─────────────────────────────────────────────────────────────────

void WebService::handleMetric(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    fill(resp, 200,
         sm.metricService().renderPrometheus(),
         "text/plain; version=0.0.4; charset=utf-8");
}

void WebService::handleHealth(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm; (void)req;
    fill(resp, 200, R"({"status":"ok","daemon":"mgmtd"})");
}

void WebService::handleLogin(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    try
    {
        const auto body     = json::parse(req.body);
        const auto username = body.at("username").get<std::string>();
        const auto password = body.at("password").get<std::string>();

        const auto result = sm.authService().login(username, password);
        if (!result.success)
            return fill(resp, 401, R"({"error":"invalid credentials"})");

        // Signal must_change so the client forces a password change before proceeding.
        const json okBody = {{"status", "ok"}, {"must_change", result.mustChange}};
        fill(resp, 200, okBody.dump());
        resp.setCookie =
            "session=" + result.sessionId + "; HttpOnly; Path=/; SameSite=Strict";
    }
    catch (const std::exception& e)
    {
        LOG_WARN("login bad request (error={})", e.what());
        fill(resp, 400, R"({"error":"bad request"})");
    }
}

void WebService::handleLogout(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    sm.authService().logout(extractSession(req));

    fill(resp, 200, R"({"status":"logged_out"})");
    resp.setCookie = "session=; Path=/; Max-Age=0";
}

void WebService::handleChangePassword(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    try
    {
        const auto body    = json::parse(req.body);
        const auto oldPass = body.at("old_password").get<std::string>();
        const auto newPass = body.at("new_password").get<std::string>();

        if (newPass.empty())
            return fill(resp, 400, R"({"error":"new password must not be empty"})");

        // Single operator account: the username is whatever the (authenticated)
        // session's admin is. Require the current password before changing it.
        const std::string& user = sm.authService().username();
        if (!sm.authService().checkPassword(user, oldPass))
            return fill(resp, 401, R"({"error":"current password is incorrect"})");

        // mgmtd is read-only w.r.t. the DB: compute the new credential and hand the
        // WRITE to engined (the single DB writer) over IPC. The HTTP server runs on the
        // single main-loop thread (io_context.poll()), so we must NOT block here — a
        // blocking confirm would stall the very loop that flushes this IPC frame,
        // deadlocking itself. Instead we send and adopt the credential optimistically;
        // engined persists it, and the DB stays the source of truth across restarts.
        const auto cred = sm.authService().makeCredential(newPass);

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
        sm.txRouter().handleIpcMessage(std::move(msg));

        // Adopt the new credential into memory immediately: subsequent logins verify
        // against it and the forced-change gate opens (m_mustChange cleared).
        sm.authService().applyCredential(cred.passwordHash, cred.salt);

        LOG_INFO("admin password change sent to engined (user={})", user);
        fill(resp, 200, R"({"status":"ok"})");
    }
    catch (const std::exception& e)
    {
        LOG_WARN("change-password bad request (error={})", e.what());
        fill(resp, 400, R"({"error":"bad request"})");
    }
}

void WebService::handleWhoami(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    // Identify the signed-in user for the UI. Prefer the per-session identity (the SSO
    // subject/email, or the local admin name); fall back to the local operator name for
    // legacy sessions minted before the session carried a username.
    std::string user = sm.authService().sessionUser(extractSession(req));
    if (user.empty())
        user = sm.authService().username();

    const json out = {{"username", user}};
    fill(resp, 200, out.dump());
}

void WebService::handleStatus(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    json body;

    body["uptime_seconds"] = 0.0;

    json daemons = json::array();

    const auto& hb = sm.heartbeatService();
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

    fill(resp, 200, body.dump());
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
    // SAML/IdP config is infrastructure set in the startup-config, not operator config;
    // it has no settings-UI tab, so don't ship it to every authenticated session.
    "auth",
};

} // namespace

void WebService::handleSettingsGet(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm; (void)req;
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

    fill(resp, 200, body.dump());
}

void WebService::handleSettingsCommit(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    auto badRequest = [&](const char* error)
    {
        fill(resp, 400, json{{"error", error}}.dump());
    };

    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return badRequest("invalid JSON body");
    }

    // Body: { "changes": [ {daemon, domain, values}, ... ] }
    if (!input.contains("changes") || !input["changes"].is_array())
        return badRequest("expected {changes: [{daemon, domain, values}]}");

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
    if (applied > 0)
    {
        const std::string payload = validChanges.dump();
        auto msg = std::make_unique<pz::ipc::IpcMessage>();
        msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
        msg->setDst(pz::ipc::IpcDaemon::Engined);
        msg->setCmd(pz::ipc::IpcCmd::SettingsCommitRequest);
        msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
        msg->setPayload(std::vector<uint8_t>(payload.begin(), payload.end()));

        sm.txRouter().handleIpcMessage(std::move(msg));
        sm.startReload();
        LOG_INFO("SettingsCommitRequest sent to engined (changes={})", applied);
    }

    const int status = (failed == 0)
        ? 200
        : (applied > 0 ? 200 : 500);

    json body;
    body["applied"]   = applied;
    body["failed"]    = failed;
    body["results"]   = json::array();
    body["reloading"] = (applied > 0);

    fill(resp, status, body.dump());
}

void WebService::handleReloadStatus(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    json body;

    const auto s = sm.reloadStatus();

    if (s == MgmtdServiceManager::ReloadStatus::Reloading)
        body["status"] = "reloading";
    else if (s == MgmtdServiceManager::ReloadStatus::Complete)
        body["status"] = "complete";
    else
        body["status"] = "idle";

    body["elapsed_ms"] = sm.reloadElapsedMs();

    fill(resp, 200, body.dump());
}

void WebService::handleCommitQueue(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    fill(resp, 200, sm.commitQueueSnapshot());
}

void WebService::handleStatic(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm;
    if (!m_cache)
        return fill(resp, 503, "static cache unavailable", "text/plain; charset=utf-8");

    // Strip query string before cache lookup (?tab=icmp, etc.)
    std::string staticPath(req.target);
    const auto qpos = staticPath.find('?');
    if (qpos != std::string::npos)
        staticPath.erase(qpos);

    auto file = m_cache->get(staticPath);
    if (!file)
        return fill(resp, 404, "not found", "text/plain; charset=utf-8");

    fill(resp, 200, std::move(file->body), std::move(file->contentType));
    // Content-addressed validator; the Handler turns a matching If-None-Match into a 304.
    resp.etag = std::move(file->etag);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool WebService::isAuthenticated(MgmtdServiceManager& sm, const Request& req) const
{
    return sm.authService().validateSession(extractSession(req));
}

bool WebService::isStaticTarget(const std::string& target)
{
    return target.rfind("/api", 0) != 0;
}

std::string WebService::extractSession(const Request& req)
{
    const std::string& cookies = req.cookie;
    if (cookies.empty()) return {};

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

// ── /api/devices ─────────────────────────────────────────────────────────────

void WebService::handleDevices(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    json body;
    json devices = json::array();
    int networkCount = 0;
    int serverCount  = 0;
    int unknownCount = 0;

    const auto groups = sm.deviceService().groups();

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

    body["summary"] = {
        {"total",   networkCount + serverCount + unknownCount},
        {"network", networkCount},
        {"server",  serverCount},
        {"unknown", unknownCount},
    };
    body["devices"] = std::move(devices);

    fill(resp, 200, body.dump());
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

void WebService::handleLogs(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm;
    const std::string& target = req.target;
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
            return fill(resp, 400, json{{"error", "unknown daemon"}}.dump());
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

    fill(resp, 200, body.dump());
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

void WebService::handleNodeMetrics(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm; (void)req;
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

    fill(resp, 200, body.dump());
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

void WebService::handleSsoInfo(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
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
    const bool adminSetup = !sm.authService().mustChangePassword();
    enabled = enabled && adminSetup;

    const json out = {{"enabled", enabled},
                      {"method", method},
                      {"label", "Sign in with Okta"},
                      {"admin_setup_required", !adminSetup}};
    fill(resp, 200, out.dump());
}

void WebService::handleSsoLogin(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    auto redirectErr = [&](const std::string& code) {
        fill(resp, 302, "", "text/plain");
        resp.location = "/index.html?sso_error=" + code;
    };

    // Defense in depth for the SSO gate: even if a stale page still shows the button,
    // refuse to start the IdP round-trip while the local admin is unprovisioned — the
    // resulting session would only 403 on every /api/*. Mirrors handleSsoInfo.
    if (sm.authService().mustChangePassword())
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

    fill(resp, 200, html, "text/html; charset=utf-8");
}

void WebService::handleSamlAcs(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    auto redirectErr = [&](const std::string& code) {
        fill(resp, 302, "", "text/plain");
        resp.location = "/index.html?sso_error=" + code;
    };

    const std::string samlResponse = ssoFormField(req.body, "SAMLResponse");
    if (samlResponse.empty())   return redirectErr("no_saml_response");

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
    sm.txRouter().handleIpcMessage(std::move(msg));

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

    fill(resp, 200, html, "text/html; charset=utf-8");
}

void WebService::handleSamlResult(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    auto jsonResp = [&](const json& j) {
        fill(resp, 200, j.dump());
    };

    const std::string& target = req.target;
    std::uint32_t ticket = 0;
    if (auto pos = target.find("ticket="); pos != std::string::npos)
        ticket = static_cast<std::uint32_t>(std::strtoul(target.c_str() + pos + 7, nullptr, 10));

    if (ticket == 0)
        return jsonResp({{"status", "error"}, {"error", "bad ticket"}});

    auto result = sm.takeSsoResult(ticket);
    if (!result)
        return jsonResp({{"status", "pending"}});

    try
    {
        const auto verdict = json::parse(*result);
        if (verdict.value("success", false))
        {
            const std::string user = verdict.value("username", "");
            const std::string sid  = sm.authService().createSsoSession(user);
            if (sid.empty())
                return jsonResp({{"status", "error"}, {"error", "session error"}});

            fill(resp, 200,
                 json({{"status", "ok"}, {"redirect", "/dashboard.html"}}).dump());
            resp.setCookie = "session=" + sid + "; HttpOnly; Path=/; SameSite=Strict";
            LOG_INFO("sso login ok (user={})", user);
            return;
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
void WebService::handleLabExport(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm;
    std::string pdf = labBase64Decode(ssoFormField(req.body, "pdf"));
    if (pdf.empty())
        return fill(resp, 400, R"({"error":"empty pdf"})");

    std::string filename = ssoFormField(req.body, "name");
    filename.erase(std::remove_if(filename.begin(), filename.end(),
        [](char c) { return c == '"' || c == '\\' || c == '/' || c == '\r' || c == '\n'; }),
        filename.end());
    if (filename.empty()) filename = "cidi-export.pdf";

    const std::size_t bytes = pdf.size();

    resp.status             = 200;
    resp.contentType        = "application/pdf";
    resp.contentDisposition = "attachment; filename=\"" + filename + "\"";
    resp.body               = std::move(pdf);

    LOG_INFO("lab cidi-export served (name={}, bytes={})", filename, bytes);
}

} // namespace pz::mgmtd
