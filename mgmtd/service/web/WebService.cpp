#include "service/web/WebService.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebAction.h"
#include "service/web/WebEvent.h"

#include "router/MgmtdTxRouter.h"

#include "config/ApiRefs.h"
#include "config/Config.h"
#include "db/Database.h"
#include "http/HttpClient.h"
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
#include <sys/statvfs.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pz::mgmtd
{

using json = nlohmann::json;

namespace
{

constexpr const char* kPublicPages[] = {
    "/", "/index.html", "/css/main.css", "/js/main.js", "/js/login.js",
};

void fill(pz::http::HttpResponse& r, int status, std::string body,
          std::string contentType = "application/json; charset=utf-8")
{
    r.status = status;
    r.contentType = std::move(contentType);
    r.body = std::move(body);
}

void unauthorized(pz::http::HttpResponse& r)
{
    fill(r, 401, R"({"error":"unauthorized"})");
}

}

void WebService::setCache(std::shared_ptr<pz::http::StaticFileCache> cache)
{
    m_cache = std::move(cache);
}

void WebService::handleEvent(MgmtdServiceManager& sm, const WebEvent& event)
{
    Response resp;
    route(sm, event.request(), resp);

    sm.postAction(std::make_unique<WebAction>(std::move(resp), event.sessionId()));
}

void WebService::handleAction(MgmtdServiceManager& sm, WebAction& action)
{
    sm.txRouter().handleHttpMessage(std::move(action.response()), action.sessionId());
}

void WebService::route(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    const std::string& target = req.target;

    LOG_TRACE("HTTP request (method={}, target={})", req.method, target);

    if (target == "/metrics" && req.method == "GET")
        return handleMetric(sm, req, resp);

    if (target == "/health" && req.method == "GET")
        return handleHealth(sm, req, resp);

    if (target == "/api/login" && req.method == "POST")
        return handleLogin(sm, req, resp);

    if (target == "/api/auth/sso/info" && req.method == "GET")
        return handleSsoInfo(sm, req, resp);
    if (target == "/api/auth/sso/login" && req.method == "GET")
        return handleSsoLogin(sm, req, resp);
    if (target == "/api/auth/saml/acs" && req.method == "POST")
        return handleSamlAcs(sm, req, resp);
    if (target.rfind("/api/auth/saml/result", 0) == 0 && req.method == "GET")
        return handleSamlResult(sm, req, resp);

    if (target == "/api/logout" && req.method == "POST")
    {
        return handleLogout(sm, req, resp);
    }

    if (target == "/api/change-password" && req.method == "POST")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleChangePassword(sm, req, resp);
    }

    if (target.rfind("/api/", 0) == 0 && isAuthenticated(sm, req) && sm.authService().mustChangePassword())
    {
        return fill(resp, 403, R"({"error":"password change required","code":"MUST_CHANGE_PASSWORD"})");
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

    if (target == "/api/settings/running-config" && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleRunningConfig(sm, req, resp);
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

    if (target == "/api/inventory/status" && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleInventoryStatus(sm, req, resp);
    }

    if (target == "/api/connector/keygen-test" && req.method == "POST")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleKeygenTest(sm, req, resp);
    }

    if (target == "/api/connector/endpoint-test" && req.method == "POST")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleEndpointTest(sm, req, resp);
    }

    if (target.rfind("/api/connector/test-result", 0) == 0 && req.method == "GET")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleApiTestResult(sm, req, resp);
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

    if (target == "/api/lab/cidi-export" && req.method == "POST")
    {
        if (!isAuthenticated(sm, req))
            return unauthorized(resp);
        return handleLabExport(sm, req, resp);
    }

    if (isStaticTarget(target))
    {
        bool isPublic = false;
        for (const auto* p : kPublicPages)
        {
            if (target == p)
            {
                isPublic = true;
                break;
            }
        }

        if (!isPublic && !isAuthenticated(sm, req))
        {
            resp.status = 302;
            resp.contentType = "text/plain; charset=utf-8";
            resp.body.clear();
            resp.location = "/index.html";
            return;
        }

        return handleStatic(sm, req, resp);
    }
}

void WebService::handleMetric(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    fill(resp, 200, sm.metricService().renderPrometheus(), "text/plain; version=0.0.4; charset=utf-8");
}

void WebService::handleHealth(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm;
    (void)req;
    fill(resp, 200, R"({"status":"ok","daemon":"mgmtd"})");
}

namespace
{

// Hands a new local credential to engined, the only database writer. Shared by the explicit
// password change and by the transparent upgrade a login performs when it meets an
// old-format hash.
void persistCredential(MgmtdServiceManager& sm, const std::string& username,
                       const AuthService::Credential& cred)
{
    const json payload = {{"username", username}, {"password_hash", cred.passwordHash}, {"salt", cred.salt}};
    const std::string payloadStr = payload.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::AdminPasswordUpdate);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payloadStr.begin(), payloadStr.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

}

void WebService::handleLogin(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    try
    {
        const auto body = json::parse(req.body);
        const auto username = body.at("username").get<std::string>();
        const auto password = body.at("password").get<std::string>();

        const auto result = sm.authService().login(username, password);
        if (result.throttled)
            return fill(resp, 429, R"({"error":"too many failed attempts — try again shortly"})");
        if (!result.success)
            return fill(resp, 401, R"({"error":"invalid credentials"})");

        // The credential verified against an outdated hash format. This is the only moment the
        // plaintext exists, so it is re-stored now rather than left to expire on its own — the
        // operator sees nothing, and the old-format row is gone after one login.
        if (result.rehashNeeded)
        {
            const auto cred = sm.authService().makeCredential(password);
            if (cred.passwordHash.empty())
            {
                LOG_WARN("credential upgrade skipped — rehash failed (user={})", username);
            }
            else
            {
                persistCredential(sm, username, cred);
                sm.authService().applyCredential(cred.passwordHash, cred.salt);
                LOG_INFO("credential upgraded to the current hash format (user={})", username);
            }
        }

        const json okBody = {{"status", "ok"}, {"must_change", result.mustChange}};
        fill(resp, 200, okBody.dump());
        resp.setCookie = "session=" + result.sessionId + "; HttpOnly; Path=/; SameSite=Strict";
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
        const auto body = json::parse(req.body);
        const auto oldPass = body.at("old_password").get<std::string>();
        const auto newPass = body.at("new_password").get<std::string>();

        if (newPass.empty())
            return fill(resp, 400, R"({"error":"new password must not be empty"})");

        const std::string& user = sm.authService().username();
        if (!sm.authService().checkPassword(user, oldPass))
            return fill(resp, 401, R"({"error":"current password is incorrect"})");

        const auto cred = sm.authService().makeCredential(newPass);
        if (cred.passwordHash.empty())
        {
            // The CSPRNG or the KDF failed. Reporting success here would leave the old password
            // in place while telling the operator it had changed.
            LOG_ERROR("password change aborted — credential could not be generated (user={})", user);
            return fill(resp, 500, R"({"error":"could not generate the new credential"})");
        }

        persistCredential(sm, user, cred);
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
                entry["name"] = d.value("name", "unknown");
                entry["status"] = d.value("status", "dead");
                entry["latency_ms"] = d.contains("latency_ms") ? d["latency_ms"] : json(nullptr);
                daemons.push_back(std::move(entry));
            }
        }
        catch (...)
        {
        }
    }

    daemons.push_back({{"name", "prometheus"}, {"status", "alive"}, {"latency_ms", nullptr}});
    daemons.push_back({{"name", "grafana"}, {"status", "alive"}, {"latency_ms", nullptr}});
    daemons.push_back({{"name", "node_exporter"}, {"status", "alive"}, {"latency_ms", nullptr}});

    body["daemons"] = std::move(daemons);

    body["events"] = json::array({
        {{"source", "mgmtd"}, {"message", "HTTPS listener online"}},
        {{"source", "engined"}, {"message", "heartbeat polling active"}},
    });

    fill(resp, 200, body.dump());
}

namespace
{

constexpr const char* kSettingsDaemons[] = {
    "engined", "authd", "icmpd", "scand", "topologyd",
};

constexpr const char* kHiddenDomains[] = {
    "bootstrap",
    "auth",
};

// ── Commit schema ────────────────────────────────────────────────────────────
// The commit endpoint is generic — any domain of a known daemon passes through — but the
// domains the UI publishes carry a declared schema, validated here so a malformed entry never
// reaches the running_config. One schema per (daemon, domain, key); everything else is opaque.
//
// Every configuration object is identified by exactly one field, `oid`: a UUID string issued at
// creation and immutable for the object's lifetime. Cross-references (site, auth_profile,
// object) carry the referent's oid.

// engined.site.devices — Devices (www/js/config.js). `device_type` is the single axis:
// ngfw is reached at its own address, prisma_access through a tenant, so the access kind is
// derived from it rather than stored twice. A device carries the TLS pin for its host but no
// credential — an API Key references the device and holds the account.
//   { oid, name, description?, site?, device_type: ngfw|prisma_access, target, fingerprint? }
// `fingerprint` is written by the API Key test once the operator confirms the certificate, not
// typed — pinning is unconditional, so there is no TLS mode to choose.
bool validDevice(const json& d)
{
    if (!d.is_object())
        return false;
    if (d.value("oid", std::string()).empty() || d.value("target", std::string()).empty())
        return false;

    const std::string deviceType = d.value("device_type", std::string());
    return deviceType == "ngfw" || deviceType == "prisma_access";
}

// engined.site.sites — Sites, one per customer (www/js/sites.js).
//   { oid, name, description? }
bool validSite(const json& s)
{
    if (!s.is_object())
        return false;
    return !s.value("oid", std::string()).empty() && !s.value("name", std::string()).empty();
}

// scand.api.api_keys — API Keys (www/js/api-keys.js). Bound to a device because a PAN-OS key is
// issued by one box and worthless on another. The password and the issued key are NOT here:
// running_config is append-versioned and shown verbatim in the review diff, so a secret written
// there would be permanent and visible. They stay in the operator's browser until the encrypted
// credential store exists, and a commit carrying one is refused below.
//   { oid, name, device, endpoint, username? }
bool validApiKey(const json& k)
{
    if (!k.is_object())
        return false;
    if (k.value("oid", std::string()).empty() || k.value("name", std::string()).empty() ||
        k.value("device", std::string()).empty())
        return false;

    const std::string endpoint = k.value("endpoint", std::string());
    if (endpoint.empty() || endpoint.front() != '/')
        return false;

    for (const auto* secret : {"password", "secret", "api_key", "key"})
    {
        if (k.contains(secret))
            return false;
    }
    return true;
}

// scand.api.endpoints — API Endpoints (www/js/endpoints.js). Device-independent on purpose: the
// definition is reusable, and a test names an API Key, which carries the device it was issued by.
// The release sits inside the path (/restapi/v10.2/…) and pretzel does not rewrite it.
//
// Path AND parameters together: an endpoint is a complete, callable request, which is why this
// page can test one on its own. Two firewalls needing different arguments (vsys1 vs vsys2) are
// two endpoints rather than one endpoint parameterised at every use — that keeps a connector a
// pure schedule, and makes a PAN-OS upgrade a matter of publishing the new release's path as a
// second endpoint and re-pointing the collections that moved.
//   { oid, name, description?, path, api_type?, params?: [{name, value}] }
bool validApiEndpoint(const json& e)
{
    if (!e.is_object())
        return false;
    if (e.value("oid", std::string()).empty() || e.value("name", std::string()).empty())
        return false;

    const std::string path = e.value("path", std::string());
    if (path.empty() || path.front() != '/')
        return false;

    if (e.contains("api_type"))
    {
        const std::string apiType = e.value("api_type", std::string());
        if (apiType != "xml" && apiType != "rest")
            return false;
    }

    if (e.contains("params"))
    {
        if (!e["params"].is_array())
            return false;
        for (const auto& p : e["params"])
        {
            if (!p.is_object() || p.value("name", std::string()).empty())
                return false;
        }
    }
    return true;
}

// scand.api.connectors — API Connectors (www/js/api-connectors.js).
//   { oid, name, description?, object, auth_profile,
//     items: [{ endpoint, poll_interval_sec, enabled }] }
//
// The collection policy for one inventory object: which credential to use against it, and which
// endpoints to poll how often. One connector per object, so a device's whole schedule is in one
// place. `object`, `auth_profile` and each item's `endpoint` are oid references; the targets are
// checked in validateApiReferences, which can see the other arrays.
bool validApiConnector(const json& c)
{
    if (!c.is_object())
        return false;

    if (c.value("oid", std::string()).empty() || c.value("object", std::string()).empty() ||
        c.value("auth_profile", std::string()).empty())
        return false;

    // Rejected explicitly rather than ignored: before connectors became schedules they carried a
    // single endpoint and its parameters inline, and silently accepting that shape would leave a
    // connector that collects nothing while looking configured.
    if (c.contains("endpoint") || c.contains("params") || c.contains("poll_interval_sec"))
        return false;

    if (!c.contains("items"))
        return false;

    if (!c["items"].is_array())
        return false;

    for (const auto& item : c["items"])
    {
        if (!item.is_object())
            return false;

        if (item.value("endpoint", std::string()).empty())
            return false;

        if (item.contains("poll_interval_sec"))
        {
            if (!item["poll_interval_sec"].is_number_integer() ||
                item["poll_interval_sec"].get<std::int64_t>() < 1)
                return false;
        }

        if (item.contains("enabled") && !item["enabled"].is_boolean())
            return false;
    }

    return true;
}

// Referential integrity across the three api arrays.
//
// A commit usually carries only one of them — the endpoints page publishes just `endpoints` —
// so the rule runs against the EFFECTIVE post-commit view: what is already stored, overlaid
// with what is arriving. That catches both directions at once: deleting an endpoint a stored
// connector still points at, and adding a connector that points at nothing.
//
// The rule itself is pz::config::checkApiReferences, which takes the assembled section and
// nothing else; only the assembling belongs here.
bool validateApiReferences(const json& values, std::string& error)
{
    json effective = pz::config::Config::serviceSection("scand", "api");
    if (!effective.is_object())
        effective = json::object();

    for (auto it = values.begin(); it != values.end(); ++it)
        effective[it.key()] = it.value();

    return pz::config::checkApiReferences(effective, error);
}

bool validateCommitValues(const std::string& daemon, const std::string& domain, const json& values,
                          std::string& error)
{
    auto validateArray = [&](const char* key, bool (*validEntry)(const json&))
    {
        if (!values.contains(key))
            return true;
        if (!values[key].is_array())
        {
            error = std::string(key) + " must be an array";
            return false;
        }
        for (const auto& entry : values[key])
        {
            if (!validEntry(entry))
            {
                error = std::string("invalid ") + key + " entry";
                return false;
            }
        }
        return true;
    };

    if (daemon == "engined" && domain == "site")
        return validateArray("sites", validSite) && validateArray("devices", validDevice);

    if (daemon == "scand" && domain == "api")
        return validateArray("api_keys", validApiKey) && validateArray("endpoints", validApiEndpoint) &&
               validateArray("connectors", validApiConnector) && validateApiReferences(values, error);

    return true;
}

}

void WebService::handleSettingsGet(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm;
    (void)req;
    json daemons = json::object();

    for (const auto* name : kSettingsDaemons)
    {
        const auto& root = pz::config::Config::daemonConfig(name);
        const auto& allService = root.value("service", json::object());

        json service = json::object();
        for (const auto& [domain, values] : allService.items())
        {
            const bool hidden = std::any_of(std::begin(kHiddenDomains), std::end(kHiddenDomains),
                                            [&](const char* d) { return domain == d; });
            if (hidden)
                continue;

            json v = values;
            if (domain == "http" && v.is_object())
                v.erase("admin");

            service[domain] = std::move(v);
        }

        daemons[name] = std::move(service);
    }

    json body;
    body["daemons"] = std::move(daemons);

    // Version of the active running-config these values came from. The browser stamps its staged
    // edits with it: if the version later goes backwards, those drafts belong to a configuration
    // lineage that no longer exists (a reset or a rollback) and must not be published.
    body["version"] = 0;
    try
    {
        const auto rows = pz::db::Database::instance().queryRows(
            "SELECT version FROM running_config WHERE state = 'active' ORDER BY version DESC LIMIT 1");
        if (!rows.empty() && !rows.front().empty())
            body["version"] = std::stoll(rows.front()[0]);
    }
    catch (const std::exception&)
    {
    }

    fill(resp, 200, body.dump());
}

// The whole active running-config, verbatim, as stored. /api/settings is a per-daemon,
// hidden-domain-filtered projection for the editors; this is the raw document the operator sees
// behind the topbar's View button. Secrets are already stripped on the way in (Config's
// redactSecretsForPersist runs at persist time), so the stored copy is safe to return as-is.
void WebService::handleRunningConfig(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm;
    (void)req;

    json body;
    try
    {
        auto& db = pz::db::Database::instance();
        const auto rows = db.queryRows("SELECT version, committed_at, config_json FROM running_config "
                                       "WHERE state = 'active' ORDER BY version DESC LIMIT 1");
        if (rows.empty() || rows.front().size() < 3)
            return fill(resp, 404, R"({"error":"no active running-config"})");

        const auto& row = rows.front();
        auto parsed = json::parse(row[2], nullptr, false);
        if (parsed.is_discarded())
            return fill(resp, 500, R"({"error":"stored running-config is not valid JSON"})");

        body["version"] = row[0];
        body["committed_at"] = row[1];
        body["config"] = std::move(parsed);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("running-config query failed: {}", e.what());
        return fill(resp, 500, R"({"error":"query failed"})");
    }

    fill(resp, 200, body.dump());
}

void WebService::handleSettingsCommit(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    auto badRequest = [&](const char* error) { fill(resp, 400, json{{"error", error}}.dump()); };

    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return badRequest("invalid JSON body");
    }

    if (!input.contains("changes") || !input["changes"].is_array())
        return badRequest("expected {changes: [{daemon, domain, values}]}");

    const json& changes = input["changes"];

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

        const bool knownDaemon = std::any_of(std::begin(kSettingsDaemons), std::end(kSettingsDaemons),
                                             [&](const char* d) { return daemon == d; });

        if (!knownDaemon)
        {
            LOG_WARN("settings-commit unknown daemon (daemon={})", daemon);
            failed++;
            continue;
        }

        std::string schemaError;
        if (!validateCommitValues(daemon, domain, values, schemaError))
        {
            LOG_WARN("settings-commit schema violation (daemon={}, domain={}, error={})", daemon, domain,
                     schemaError);
            failed++;
            continue;
        }

        validChanges.push_back(change);
    }

    const int applied = static_cast<int>(validChanges.size());

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

    const int status = (failed == 0) ? 200 : (applied > 0 ? 200 : 500);

    json body;
    body["applied"] = applied;
    body["failed"] = failed;
    body["results"] = json::array();
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

    std::string staticPath(req.target);
    const auto qpos = staticPath.find('?');
    if (qpos != std::string::npos)
        staticPath.erase(qpos);

    auto file = m_cache->get(staticPath);
    if (!file)
        return fill(resp, 404, "not found", "text/plain; charset=utf-8");

    fill(resp, 200, std::move(file->body), std::move(file->contentType));
    resp.etag = std::move(file->etag);
}

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
    if (cookies.empty())
        return {};

    const std::string key = "session=";
    auto pos = cookies.find(key);
    while (pos != std::string::npos)
    {
        if (pos == 0 || cookies[pos - 1] == ' ' || cookies[pos - 1] == ';')
            break;
        pos = cookies.find(key, pos + 1);
    }
    if (pos == std::string::npos)
        return {};

    auto end = cookies.find(';', pos);
    if (end == std::string::npos)
        end = cookies.size();

    return cookies.substr(pos + key.size(), end - (pos + key.size()));
}


void WebService::handleInventoryStatus(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm;
    (void)req;

    // engined projects the configured devices into `devices` and marks each NGFW (IP-based)
    // row 'active' when ICMP answers; return the set of currently-active targets (IPs).
    json alive = json::array();
    try
    {
        auto& db = pz::db::Database::instance();
        for (const auto& row : db.queryRows("SELECT target FROM devices WHERE status = 'active' AND target IS NOT NULL"))
        {
            if (!row.empty() && !row[0].empty())
                alive.push_back(row[0]);
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("device status query failed: {}", e.what());
    }

    fill(resp, 200, json{{"alive", std::move(alive)}}.dump());
}

// ── API connector tests ──────────────────────────────────────────────────────
// Two gated checks the operator runs while creating a connector:
//   1. keygen   — do these credentials produce an API key on this device?
//   2. endpoint — does the path the operator typed answer with that key?
// Both are separate because pretzel manages many customers' firewalls: the operator supplies
// the endpoint explicitly (PAN-OS versions differ per customer, and the REST path carries the
// version), so a working credential and a working path are independent failures worth
// distinguishing.
//
// Neither call happens here. scand owns the device exchange: it is the daemon that will poll
// these connectors on a schedule, and a test exercising a different code path than the
// collector would not be testing much. mgmtd validates the request, forwards the target — which
// the operator may not have committed yet, so the whole thing travels in the payload — and
// correlates scand's reply by seqNo, the same shape as the SAML ACS delegation to authd. The
// browser still polls /api/connector/test-result.
namespace
{

// The path the device is actually asked for: the endpoint plus its query parameters, encoded the
// same way scand builds the request (ApiService::runEndpointCall). The endpoint-page test carries
// its parameters as a separate array, so the bare `endpoint` field understates what was called —
// this reunites them so the log shows the full request the operator entered. The connector test
// already bakes its parameters into `endpoint` and sends an empty array, so both flows log the
// same shape.
std::string fullEndpoint(const json& input)
{
    std::string path = input.value("endpoint", std::string());
    for (const auto& p : input.value("params", json::array()))
    {
        if (!p.is_object())
            continue;
        const std::string name = p.value("name", std::string());
        if (name.empty())
            continue;
        path += (path.find('?') == std::string::npos) ? '?' : '&';
        path += pz::http::urlEncode(name) + "=" + pz::http::urlEncode(p.value("value", std::string()));
    }
    return path;
}

// Rejects what can be judged without touching the device, so a typo answers immediately
// instead of costing an IPC round trip and a ticket poll. scand re-checks defensively.
std::string connectorTestInputError(const json& input, bool endpointMode)
{
    if (input.value("target", std::string()).empty())
        return "target is required";

    // A password is only one of two ways in. scand may already hold the key issued for this
    // profile, in which case it needs no credential at all — and only scand knows that, so the
    // check here is "is there ANY way to authenticate", not "is there a password".
    //
    // is_object() is checked rather than assumed: nlohmann's value() throws when called on a
    // non-object, and a malformed body must produce a 400, not an exception.
    const auto secrets = input.value("secrets", json::object());
    const bool haveCredential = secrets.is_object() && !secrets.value("username", std::string()).empty() &&
                                !secrets.value("password", std::string()).empty();
    const bool namesProfile = !input.value("api_key_oid", std::string()).empty();

    // Generating a key is the one case a password is unavoidable — that exchange IS the
    // password. Calling an endpoint can instead ride on a key already issued for the profile.
    if (!endpointMode && !haveCredential)
        return "generating a key needs the username and password — enter them on the API Key page";

    if (endpointMode && !haveCredential && !namesProfile)
        return "this test has no API key to use — pick one on the connector, or enter the "
               "password on the API Key page and generate a key once";

    if (!endpointMode)
        return {};

    const std::string endpoint = input.value("endpoint", std::string());
    if (endpoint.empty() || endpoint.front() != '/')
        return "endpoint must be a path starting with /";

    const std::string apiType = input.value("api_type", std::string("rest"));
    if (apiType != "xml" && apiType != "rest")
        return "api_type must be xml or rest";

    // Query parameters are entered as name/value pairs rather than typed into the path, so the
    // operator does not have to hand-encode them (PAN-OS REST requires ?location=, and XML API
    // cmd= values contain characters that must be escaped).
    if (!input.value("params", json::array()).is_array())
        return "params must be an array of {name, value}";

    return {};
}

void sendConnectorTest(MgmtdServiceManager& sm, std::uint32_t ticket, json input, const char* mode)
{
    input["mode"] = mode;
    const std::string payload = input.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Scand);
    msg->setCmd(pz::ipc::IpcCmd::ApiConnectorTestRequest);
    msg->setSeqNo(ticket);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

}

void WebService::handleKeygenTest(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    if (const auto err = connectorTestInputError(input, false); !err.empty())
    {
        LOG_DEBUG("keygen test rejected pre-flight (host={}, reason={})",
                  input.value("target", std::string()), err);
        return fill(resp, 400, json{{"error", err}}.dump());
    }

    const std::uint32_t ticket = m_apiTestTicket++;
    const std::string host = input.value("target", std::string());
    sendConnectorTest(sm, ticket, std::move(input), "keygen");

    LOG_INFO("keygen test delegated to scand (ticket={}, host={})", ticket, host);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void WebService::handleEndpointTest(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    if (const auto err = connectorTestInputError(input, true); !err.empty())
    {
        LOG_DEBUG("endpoint test rejected pre-flight (host={}, endpoint={}, reason={})",
                  input.value("target", std::string()), fullEndpoint(input), err);
        return fill(resp, 400, json{{"error", err}}.dump());
    }

    const std::uint32_t ticket = m_apiTestTicket++;
    const std::string host = input.value("target", std::string());
    // The full request as entered on the frontend: path + query parameters, not just the bare path.
    const std::string endpoint = fullEndpoint(input);
    sendConnectorTest(sm, ticket, std::move(input), "endpoint");

    LOG_INFO("endpoint test delegated to scand (ticket={}, host={}, endpoint={})", ticket, host, endpoint);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void WebService::handleApiTestResult(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    std::uint32_t ticket = 0;
    if (auto pos = req.target.find("ticket="); pos != std::string::npos)
        ticket = static_cast<std::uint32_t>(std::strtoul(req.target.c_str() + pos + 7, nullptr, 10));

    if (ticket == 0)
        return fill(resp, 400, R"({"error":"bad ticket"})");

    auto result = sm.takeApiTestResult(ticket);
    if (!result)
        return fill(resp, 200, R"({"status":"pending"})");

    // The worker stored a complete result document; tag it and hand it over as-is.
    json body = json::parse(*result, nullptr, false);
    if (body.is_discarded())
        return fill(resp, 500, R"({"status":"done","ok":false,"message":"malformed test result"})");

    body["status"] = "done";
    fill(resp, 200, body.dump());
}

namespace
{
std::string queryParam(const std::string& target, const std::string& key)
{
    const auto q = target.find('?');
    if (q == std::string::npos)
        return {};
    std::string qs = target.substr(q + 1);
    std::istringstream ss(qs);
    std::string token;
    while (std::getline(ss, token, '&'))
    {
        const auto eq = token.find('=');
        if (eq == std::string::npos)
            continue;
        if (token.substr(0, eq) == key)
            return token.substr(eq + 1);
    }
    return {};
}

std::vector<std::string> tailFile(const std::string& path, int maxLines)
{
    std::ifstream f(path);
    if (!f.is_open())
        return {};
    std::string line;
    if (maxLines <= 0)
    {
        std::vector<std::string> all;
        while (std::getline(f, line))
            all.push_back(line);
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
const std::vector<std::string> kKnownDaemons = {"ipcd", "engined", "mgmtd", "authd", "icmpd", "scand", "topologyd"};
}

void WebService::handleLogs(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm;
    const std::string& target = req.target;
    std::string daemon = queryParam(target, "daemon");
    const int maxLines = [&]
    {
        const auto s = queryParam(target, "lines");
        if (s.empty())
            return 0;
        const int n = std::stoi(s);
        return (n <= 0) ? 0 : n;
    }();

    if (!daemon.empty())
    {
        const bool known =
            std::any_of(kKnownDaemons.begin(), kKnownDaemons.end(), [&](const std::string& d) { return d == daemon; });
        if (!known)
            return fill(resp, 400, json{{"error", "unknown daemon"}}.dump());
    }

    json body;

    if (!daemon.empty())
    {
        const std::string path = kLogDir + "/" + daemon + ".log";
        const auto lines = tailFile(path, maxLines);
        json arr = json::array();
        for (const auto& l : lines)
            arr.push_back(l);
        body["daemon"] = daemon;
        body["lines"] = std::move(arr);
    }
    else
    {
        json all = json::object();
        for (const auto& d : kKnownDaemons)
        {
            const std::string path = kLogDir + "/" + d + ".log";
            const auto lines = tailFile(path, 50);
            json arr = json::array();
            for (const auto& l : lines)
                arr.push_back(l);
            all[d] = std::move(arr);
        }
        body["daemons"] = std::move(all);
    }

    fill(resp, 200, body.dump());
}

namespace
{
struct CpuSnapshot
{
    uint64_t total{0}, idle{0};
};

CpuSnapshot readCpuSnapshot()
{
    std::ifstream f("/proc/stat");
    std::string token;
    CpuSnapshot snap;
    if (!f.is_open())
        return snap;
    f >> token;
    std::vector<uint64_t> vals;
    uint64_t v;
    while (vals.size() < 10 && f >> v)
        vals.push_back(v);
    if (vals.size() >= 4)
    {
        snap.idle = vals[3] + (vals.size() > 4 ? vals[4] : 0);
        snap.total = 0;
        for (auto x : vals)
            snap.total += x;
    }
    return snap;
}

double readMemPct()
{
    std::ifstream f("/proc/meminfo");
    if (!f.is_open())
        return 0.0;
    uint64_t total = 0, avail = 0;
    std::string key;
    uint64_t val;
    std::string unit;
    while (f >> key >> val)
    {
        f >> unit;
        if (key == "MemTotal:")
            total = val;
        if (key == "MemAvailable:")
            avail = val;
        if (total && avail)
            break;
    }
    return total ? 100.0 * (total - avail) / total : 0.0;
}

double readDiskPct(const char* path = "/")
{
    struct statvfs st
    {
    };
    if (::statvfs(path, &st) != 0)
        return 0.0;
    const uint64_t total = static_cast<uint64_t>(st.f_blocks) * st.f_frsize;
    const uint64_t avail = static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
    return total ? 100.0 * (total - avail) / total : 0.0;
}

CpuSnapshot s_cpuPrev;
double s_cpuPct{0.0};
}

void WebService::handleNodeMetrics(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)sm;
    (void)req;
    const auto cur = readCpuSnapshot();
    if (s_cpuPrev.total > 0 && cur.total > s_cpuPrev.total)
    {
        const uint64_t dt = cur.total - s_cpuPrev.total;
        const uint64_t di = cur.idle - s_cpuPrev.idle;
        s_cpuPct = 100.0 * (dt - di) / dt;
    }
    s_cpuPrev = cur;

    json body;
    body["cpu_pct"] = std::round(s_cpuPct * 10) / 10.0;
    body["mem_pct"] = std::round(readMemPct() * 10) / 10.0;
    body["disk_pct"] = std::round(readDiskPct() * 10) / 10.0;

    fill(resp, 200, body.dump());
}

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
    for (std::size_t i = 0; i < nBytes * 2; ++i)
        s.push_back(hex[dist(gen)]);
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
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    const auto* d = reinterpret_cast<const unsigned char*>(in.data());
    std::size_t len = in.size(), i = 0;
    for (; i + 2 < len; i += 3)
    {
        std::uint32_t n = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back(tbl[n & 63]);
    }
    if (i < len)
    {
        std::uint32_t n = d[i] << 16;
        if (i + 1 < len)
            n |= d[i + 1] << 8;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back((i + 1 < len) ? tbl[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

std::string ssoUrlDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (c == '+')
        {
            out.push_back(' ');
        }
        else if (c == '%' && i + 2 < s.size())
        {
            auto hexv = [](char h) -> int
            {
                if (h >= '0' && h <= '9')
                    return h - '0';
                if (h >= 'a' && h <= 'f')
                    return h - 'a' + 10;
                if (h >= 'A' && h <= 'F')
                    return h - 'A' + 10;
                return -1;
            };
            int hi = hexv(s[i + 1]), lo = hexv(s[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            }
            else
                out.push_back(c);
        }
        else
            out.push_back(c);
    }
    return out;
}

std::string ssoFormField(const std::string& body, const std::string& key)
{
    const std::string pfx = key + "=";
    std::size_t pos = 0;
    while (pos < body.size())
    {
        std::size_t amp = body.find('&', pos);
        std::string pair = body.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (pair.rfind(pfx, 0) == 0)
            return ssoUrlDecode(pair.substr(pfx.size()));
        if (amp == std::string::npos)
            break;
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
        case '&':
            out += "&amp;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out.push_back(c);
        }
    }
    return out;
}

nlohmann::json ssoAuthConfig()
{
    const auto& root = pz::config::Config::daemonConfig("authd");
    return root.value("service", nlohmann::json::object()).value("auth", nlohmann::json::object());
}

std::string labBase64Decode(const std::string& in)
{
    auto val = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };
    std::string out;
    out.reserve(in.size() * 3 / 4);
    int buf = 0, bits = 0;
    for (char c : in)
    {
        if (c == '=')
            break;
        const int v = val(c);
        if (v < 0)
            continue;
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

}

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
    auto redirectErr = [&](const std::string& code)
    {
        fill(resp, 302, "", "text/plain");
        resp.location = "/index.html?sso_error=" + code;
    };

    if (sm.authService().mustChangePassword())
        return redirectErr("setup_required");

    const auto auth = ssoAuthConfig();
    if (auth.value("method", std::string("oidc")) != "saml")
        return redirectErr("not_configured");

    const auto saml = auth.value("saml", json::object());
    if (!saml.value("enabled", false))
        return redirectErr("disabled");

    const std::string idp = saml.value("idp_sso_url", "");
    const std::string sp = saml.value("sp_entity_id", "");
    const std::string acs = saml.value("acs_url", "");
    if (idp.empty() || acs.empty())
        return redirectErr("misconfigured");

    const std::string id = "_" + ssoRandomHex(16);
    const std::string xml = "<samlp:AuthnRequest xmlns:samlp=\"urn:oasis:names:tc:SAML:2.0:protocol\""
                            " xmlns:saml=\"urn:oasis:names:tc:SAML:2.0:assertion\""
                            " ID=\"" +
                            id + "\" Version=\"2.0\" IssueInstant=\"" + ssoUtcNow() +
                            "\""
                            " Destination=\"" +
                            idp +
                            "\""
                            " ProtocolBinding=\"urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST\""
                            " AssertionConsumerServiceURL=\"" +
                            acs +
                            "\">"
                            "<saml:Issuer>" +
                            sp + "</saml:Issuer></samlp:AuthnRequest>";

    const std::string html = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Redirecting…</title></head>"
                             "<body onload=\"document.forms[0].submit()\">"
                             "<form method=\"POST\" action=\"" +
                             ssoHtmlAttr(idp) +
                             "\">"
                             "<input type=\"hidden\" name=\"SAMLRequest\" value=\"" +
                             ssoHtmlAttr(ssoBase64(xml)) +
                             "\"/>"
                             "<noscript><button type=\"submit\">Continue to Okta</button></noscript>"
                             "</form></body></html>";

    fill(resp, 200, html, "text/html; charset=utf-8");
}

void WebService::handleSamlAcs(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    auto redirectErr = [&](const std::string& code)
    {
        fill(resp, 302, "", "text/plain");
        resp.location = "/index.html?sso_error=" + code;
    };

    const std::string samlResponse = ssoFormField(req.body, "SAMLResponse");
    if (samlResponse.empty())
        return redirectErr("no_saml_response");

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

    const std::string t = std::to_string(ticket);
    const std::string html =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Signing in…</title></head>"
        "<body><p style=\"font-family:sans-serif\">Signing in…</p><script>"
        "(function(){var t=" +
        t +
        ",n=0;"
        "function go(){n++;if(n>40){location.href='/index.html?sso_error=timeout';return;}"
        "fetch('/api/auth/saml/result?ticket='+t).then(function(r){return r.json();})"
        ".then(function(d){"
        "if(d.status==='ok'){location.href=d.redirect||'/home.html';}"
        "else if(d.status==='error'){location.href='/index.html?sso_error='+encodeURIComponent(d.error||'failed');}"
        "else{setTimeout(go,600);}"
        "}).catch(function(){setTimeout(go,900);});}"
        "go();})();"
        "</script></body></html>";

    fill(resp, 200, html, "text/html; charset=utf-8");
}

void WebService::handleSamlResult(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    auto jsonResp = [&](const json& j) { fill(resp, 200, j.dump()); };

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
            const std::string sid = sm.authService().createSsoSession(user);
            if (sid.empty())
                return jsonResp({{"status", "error"}, {"error", "session error"}});

            fill(resp, 200, json({{"status", "ok"}, {"redirect", "/home"}}).dump());
            resp.setCookie = "session=" + sid + "; HttpOnly; Path=/; SameSite=Strict";
            LOG_INFO("sso login ok (user={})", user);
            return;
        }
        return jsonResp({{"status", "error"}, {"error", verdict.value("error", std::string("verification failed"))}});
    }
    catch (const std::exception& e)
    {
        LOG_WARN("sso result parse error: {}", e.what());
        return jsonResp({{"status", "error"}, {"error", "bad result"}});
    }
}

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
    if (filename.empty())
        filename = "cidi-export.pdf";

    const std::size_t bytes = pdf.size();

    resp.status = 200;
    resp.contentType = "application/pdf";
    resp.contentDisposition = "attachment; filename=\"" + filename + "\"";
    resp.body = std::move(pdf);

    LOG_INFO("lab cidi-export served (name={}, bytes={})", filename, bytes);
}

}
