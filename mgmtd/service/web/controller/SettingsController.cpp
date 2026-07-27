#include "service/web/controller/SettingsController.h"

#include "service/MgmtdServiceManager.h"

#include "service/web/WebResponse.h"
#include "service/web/WebRouter.h"

#include "router/MgmtdTxRouter.h"

#include "config/ApiRefs.h"
#include "config/Config.h"
#include "db/Database.h"
#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace pz::mgmtd
{

namespace
{

using json = nlohmann::json;

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
// ngfw is reached at its own address, sase through a tenant, so the access kind is
// derived from it rather than stored twice. A device carries the TLS pin for its host but no
// credential — an API Key references the device and holds the account.
//   { oid, name, description?, site?, device_type: ngfw|sase, target, fingerprint? }
// `fingerprint` is written by the API Key test once the operator confirms the certificate, not
// typed — pinning is unconditional, so there is no TLS mode to choose.
bool validDevice(const json& d)
{
    if (!d.is_object())
        return false;
    if (d.value("oid", std::string()).empty() || d.value("target", std::string()).empty())
        return false;

    const std::string deviceType = d.value("device_type", std::string());
    // "prisma_access" accepted as the pre-rename alias so a stale draft still commits; the frontend
    // normalizes it to "sase" and the schema migration rewrites persisted rows.
    return deviceType == "ngfw" || deviceType == "sase" || deviceType == "prisma_access";
}

// engined.site.sites — Sites, one per customer (www/js/sites.js).
//   { oid, name, description? }
bool validSite(const json& s)
{
    if (!s.is_object())
        return false;
    return !s.value("oid", std::string()).empty() && !s.value("name", std::string()).empty();
}

// scand.api.api_credentials — API Keys (www/js/api-keys.js). Bound to a device because a PAN-OS key is
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

    // The endpoint is operator-entered and interpreted per device type — an NGFW device path
    // ("/api/…") or a SASE auth host+path ("auth.…/oauth2/access_token"). Both shapes (and empty) are
    // accepted here; the frontend validates the shape per type and scand parses it.

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
        std::size_t idx = 0;
        for (const auto& entry : values[key])
        {
            if (!validEntry(entry))
            {
                const std::string name = entry.is_object() ? entry.value("name", std::string()) : std::string();
                error = "invalid " + std::string(key) + " entry #" + std::to_string(idx) +
                        (name.empty() ? "" : " (\"" + name + "\")");
                return false;
            }
            ++idx;
        }
        return true;
    };

    if (daemon == "engined" && domain == "site")
        return validateArray("sites", validSite) && validateArray("devices", validDevice);

    if (daemon == "scand" && domain == "api")
        return validateArray("api_credentials", validApiKey) && validateArray("endpoints", validApiEndpoint) &&
               validateArray("connectors", validApiConnector) && validateApiReferences(values, error);

    return true;
}

void handleSettingsGet(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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
void handleRunningConfig(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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

void handleSettingsCommit(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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
    json results = json::array();   // per-change outcome, returned to the browser and used below
    int failed = 0;

    // Record a per-change rejection: logged (so the reason survives in mgmtd.log) and returned.
    auto reject = [&](const std::string& daemon, const std::string& domain, const std::string& why) {
        LOG_WARN("settings-commit rejected (daemon={}, domain={}, reason={})", daemon, domain, why);
        results.push_back({{"daemon", daemon}, {"domain", domain}, {"ok", false}, {"error", why}});
        failed++;
    };

    for (const auto& change : changes)
    {
        if (!change.contains("daemon") || !change.contains("domain") || !change.contains("values"))
        {
            reject(change.value("daemon", ""), change.value("domain", ""), "entry is missing daemon/domain/values");
            continue;
        }

        const std::string daemon = change.value("daemon", "");
        const std::string domain = change.value("domain", "");
        const json& values = change["values"];

        if (!values.is_object())
        {
            reject(daemon, domain, "values is not an object");
            continue;
        }

        const bool knownDaemon = std::any_of(std::begin(kSettingsDaemons), std::end(kSettingsDaemons),
                                             [&](const char* d) { return daemon == d; });

        if (!knownDaemon)
        {
            reject(daemon, domain, "unknown daemon");
            continue;
        }

        std::string schemaError;
        if (!validateCommitValues(daemon, domain, values, schemaError))
        {
            reject(daemon, domain, schemaError.empty() ? "schema validation failed" : schemaError);
            continue;
        }

        results.push_back({{"daemon", daemon}, {"domain", domain}, {"ok", true}});
        validChanges.push_back(change);
    }

    const int applied = static_cast<int>(validChanges.size());
    if (failed > 0)
        LOG_WARN("settings-commit: {} change(s) rejected, {} accepted", failed, applied);

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
    body["results"] = std::move(results);
    body["reloading"] = (applied > 0);
    if (failed > 0 && applied == 0)
        body["error"] = "no change was accepted — see results";

    fill(resp, status, body.dump());
}

void handleReloadStatus(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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

void handleCommitQueue(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)req;
    fill(resp, 200, sm.commitQueueSnapshot());
}

// ── Saved configurations (named running-config snapshots on the appliance) ─────────────────
// Plain files under <config-dir>/saved-configs. They survive `pretzel reset` (which only drops DB
// tables) and a redeploy (which rewrites only startup-config.json), so they are the durable on-box
// backup. No secrets involved — the running-config is already redacted at persist time.

std::string savedConfigDir()
{
    const char* env = std::getenv("PRETZEL_CONFIG_DIR");
    return std::string(env && *env ? env : "/etc/pretzel") + "/saved-configs";
}

// A safe "<name>.json" path, or "" if the operator's name is unusable. Guards against path
// traversal: only [A-Za-z0-9._-], no leading dot, length-capped, no directory separators.
std::string savedConfigFile(std::string name)
{
    const std::string ext = ".json";
    if (name.size() >= ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
        name.erase(name.size() - ext.size());   // tolerate an entered ".json"
    if (name.empty() || name.size() > 100 || name.front() == '.')
        return "";
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-'))
            return "";
    return savedConfigDir() + "/" + name + ext;
}

std::string activeRunningConfigJson()
{
    const auto rows = pz::db::Database::instance().queryRows(
        "SELECT config_json FROM running_config WHERE state='active' ORDER BY version DESC LIMIT 1");
    if (rows.empty() || rows.front().empty())
        return "";
    return rows.front()[0];
}

void handleSaveConfig(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    const std::string path = savedConfigFile(input.value("name", std::string()));
    if (path.empty())
        return fill(resp, 400, R"({"error":"invalid name — use letters, digits, dot, dash or underscore"})");

    // Two callers: Save persists the live running-config (no "content"); Import persists a document
    // the browser uploaded (its raw text in "content"). An uploaded document is validated as JSON so
    // a saved file is always loadable later.
    std::string cfg;
    if (input.contains("content"))
    {
        cfg = input.value("content", std::string());
        if (json::parse(cfg, nullptr, false).is_discarded())
            return fill(resp, 400, R"({"error":"uploaded content is not valid JSON"})");
    }
    else
    {
        cfg = activeRunningConfigJson();
        if (cfg.empty())
            return fill(resp, 404, R"({"error":"no active running-config to save"})");
    }

    std::error_code ec;
    std::filesystem::create_directories(savedConfigDir(), ec);

    // Write to a temp then rename so a reader never sees a half-written document.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f)
            return fill(resp, 500, R"({"error":"could not open file for writing"})");
        f << cfg;
        if (!f)
        {
            std::filesystem::remove(tmp, ec);
            return fill(resp, 500, R"({"error":"write failed"})");
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        return fill(resp, 500, R"({"error":"rename failed"})");
    }

    LOG_INFO("running-config saved to {}", path);
    fill(resp, 200, json{{"ok", true}}.dump());
}

void handleSavedConfigs(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    (void)req;

    json out = json::array();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(savedConfigDir(), ec))
    {
        if (ec)
            break;
        const auto& p = entry.path();
        if (!entry.is_regular_file() || p.extension() != ".json")
            continue;
        struct ::stat st{};
        std::uint64_t mtime = 0, bytes = 0;
        if (::stat(p.c_str(), &st) == 0)
        {
            mtime = static_cast<std::uint64_t>(st.st_mtime);
            bytes = static_cast<std::uint64_t>(st.st_size);
        }
        out.push_back({{"name", p.stem().string()}, {"saved_at", mtime}, {"bytes", bytes}});
    }
    std::sort(out.begin(), out.end(),
              [](const json& a, const json& b) { return a.value("saved_at", 0ull) > b.value("saved_at", 0ull); });

    fill(resp, 200, out.dump());
}

// Returns the raw saved document so the browser can apply it through the same commit path Import
// uses. Selected from the list, so the name is already one of ours; still validated defensively.
void handleSavedConfigContent(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;

    std::string name;
    if (auto pos = req.target.find("name="); pos != std::string::npos)
    {
        name = req.target.substr(pos + 5);
        if (auto amp = name.find('&'); amp != std::string::npos)
            name.erase(amp);
    }

    const std::string path = savedConfigFile(name);
    if (path.empty())
        return fill(resp, 400, R"({"error":"invalid name"})");

    std::ifstream f(path);
    if (!f)
        return fill(resp, 404, R"({"error":"not found"})");

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    fill(resp, 200, content);   // already a JSON config document
}

}

void SettingsController::registerRoutes(WebRouter& router)
{
    using Access = WebRouter::Access;

    router.get("/api/settings", Access::Authenticated, &handleSettingsGet);
    router.get("/api/settings/running-config", Access::Authenticated, &handleRunningConfig);
    router.post("/api/settings/commit", Access::Authenticated, &handleSettingsCommit);
    router.get("/api/settings/reload-status", Access::Authenticated, &handleReloadStatus);
    router.get("/api/settings/commit-queue", Access::Authenticated, &handleCommitQueue);
    router.post("/api/settings/save-config", Access::Authenticated, &handleSaveConfig);
    router.get("/api/settings/saved-configs", Access::Authenticated, &handleSavedConfigs);
    router.getPrefix("/api/settings/saved-config-content", Access::Authenticated, &handleSavedConfigContent);
}

}
