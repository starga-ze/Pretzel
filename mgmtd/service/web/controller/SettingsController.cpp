#include "service/web/controller/SettingsController.h"

#include "service/MgmtdServiceManager.h"

#include "service/web/WebUtil.h"
#include "service/web/controller/SettingsValidation.h"

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
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pz::mgmtd
{

using json = nlohmann::json;


void SettingsController::get(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    (void)req;
    json scopes = json::object();

    for (const auto* scopeName : settings::kSettingsScopes)
    {
        const auto& stored = pz::config::Config::scopeConfig(scopeName);

        json projected = json::object();
        for (const auto& [domain, values] : stored.items())
        {
            if (domain.rfind("//", 0) == 0)
                continue;

            const bool hidden = std::any_of(std::begin(settings::kHiddenDomains), std::end(settings::kHiddenDomains),
                                            [&](const char* d) { return domain == d; });
            if (hidden)
                continue;

            json v = values;
            if (domain == "console" && v.is_object())
                v.erase("admin");

            projected[domain] = std::move(v);
        }

        scopes[scopeName] = std::move(projected);
    }

    json body;
    body["scopes"] = std::move(scopes);

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

// The whole active running-config, verbatim, as stored. /api/settings is a per-scope,
// hidden-domain-filtered projection for the editors; this is the raw document the operator sees
// behind the topbar's View button. Secrets are already stripped on the way in (Config's
// redactSecretsForPersist runs at persist time), so the stored copy is safe to return as-is.
void SettingsController::runningConfig(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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

void SettingsController::commit(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    auto badRequest = [&](const char* error) { fill(resp, 400, json{{"error", error}}.dump()); };

    json input;
    if (!parseBody(req, resp, input))
        return;

    // Accounts are the one domain a commit may not carry unless the person publishing it may
    // manage accounts. Everything else on this appliance is open to both roles; this is not,
    // because a user who could publish `pretzel.user` could make themselves an admin.
    //
    // Refused for the whole batch rather than by dropping the entry: a partial publish is exactly
    // what the batch rule below exists to prevent, and an operator who staged an account change
    // they may not make should be told, not quietly given the rest.
    if (input.contains("changes") && input["changes"].is_array()
        && !sm.authService().sessionIsAdmin(sessionCookie(req)))
    {
        for (const auto& c : input["changes"])
        {
            if (c.is_object() && c.value("scope", std::string()) == pz::config::scope::kPretzel
                && c.value("domain", std::string()) == "user")
            {
                LOG_WARN("settings-commit rejected: accounts may only be published by an admin "
                         "(user={})", sm.authService().sessionUser(sessionCookie(req)));
                return fill(resp, 403,
                            R"({"error":"only an admin may change accounts","code":"FORBIDDEN"})");
            }
        }
    }

    if (!input.contains("changes") || !input["changes"].is_array())
        return badRequest("expected {changes: [{scope, domain, values}]}");

    const json& changes = input["changes"];

    json validChanges = json::array();
    json results = json::array();   // per-change outcome, returned to the browser and used below
    std::vector<std::size_t> resultOf;   // parallel to validChanges: where its result entry lives
    int failed = 0;

    // Record a per-change rejection: logged (so the reason survives in mgmtd.log) and returned.
    auto reject = [&](const std::string& scopeName, const std::string& domain, const std::string& why) {
        LOG_WARN("settings-commit rejected (scope={}, domain={}, reason={})", scopeName, domain, why);
        results.push_back({{"scope", scopeName}, {"domain", domain}, {"ok", false}, {"error", why}});
        failed++;
    };

    for (const auto& change : changes)
    {
        if (!change.contains("scope") || !change.contains("domain") || !change.contains("values"))
        {
            reject(change.value("scope", ""), change.value("domain", ""), "entry is missing scope/domain/values");
            continue;
        }

        const std::string scopeName = change.value("scope", "");
        const std::string domain = change.value("domain", "");
        const json& values = change["values"];

        if (!values.is_object())
        {
            reject(scopeName, domain, "values is not an object");
            continue;
        }

        const bool knownScope = std::any_of(std::begin(settings::kSettingsScopes), std::end(settings::kSettingsScopes),
                                            [&](const char* s) { return scopeName == s; });

        if (!knownScope)
        {
            reject(scopeName, domain, "unknown scope");
            continue;
        }

        std::string schemaError;
        if (!settings::validateCommitShape(scopeName, domain, values, schemaError))
        {
            reject(scopeName, domain, schemaError.empty() ? "schema validation failed" : schemaError);
            continue;
        }

        results.push_back({{"scope", scopeName}, {"domain", domain}, {"ok", true}});
        resultOf.push_back(results.size() - 1);
        validChanges.push_back(change);
    }

    // Reference pass, on the merged view of each domain — see validateCommitRefs. Only worth running
    // on a batch that is otherwise sound: merging values from an entry already known to be malformed
    // would report a dangling reference the operator cannot act on.
    if (failed == 0)
    {
        std::map<std::pair<std::string, std::string>, json> mergedByDomain;
        for (const auto& c : validChanges)
        {
            json& dst = mergedByDomain[{c.value("scope", std::string()), c.value("domain", std::string())}];
            if (!dst.is_object())
                dst = json::object();
            // Key-level replacement, matching how the values are assembled for the check itself: an
            // editor publishes a whole array under its own key and owns that key completely.
            for (const auto& [key, value] : c["values"].items())
                dst[key] = value;
        }

        for (const auto& [target, values] : mergedByDomain)
        {
            std::string refError;
            if (settings::validateCommitRefs(target.first, target.second, values, refError))
                continue;

            if (refError.empty())
                refError = "reference check failed";

            LOG_WARN("settings-commit rejected (scope={}, domain={}, reason={})", target.first, target.second,
                     refError);

            // It is the combination that broke the reference, not one entry in it, so every change
            // aimed at this domain is marked — the operator needs to see all the parts involved to
            // know which one to correct.
            for (std::size_t i = 0; i < validChanges.size(); ++i)
            {
                if (validChanges[i].value("scope", std::string()) != target.first ||
                    validChanges[i].value("domain", std::string()) != target.second)
                    continue;

                results[resultOf[i]]["ok"] = false;
                results[resultOf[i]]["error"] = refError;
                failed++;
            }
        }
    }

    const int applied = static_cast<int>(validChanges.size());

    // All or nothing. A batch is one operator action spread across several editors and the parts are
    // not independent — a connector references an endpoint staged beside it. Publishing only the half
    // that validated leaves a configuration nobody asked for, and because the browser clears every
    // staged draft the moment anything is applied, the rejected half is gone with no way to retry it.
    // That is how a commit could appear to succeed and silently lose an edit.
    const bool accepted = (failed == 0 && applied > 0);

    if (failed > 0)
        LOG_WARN("settings-commit: {} change(s) rejected of {} — batch not published", failed,
                 static_cast<int>(changes.size()));

    if (accepted)
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

    // 409, not 200: the request was understood but conflicts with the configuration it would have
    // produced. The browser reads `results` for the per-change reason either way — the code only has
    // to be un-2xx so a rejection can never be mistaken for a publish.
    const int status = accepted ? 200 : 409;

    json body;
    body["applied"] = accepted ? applied : 0;
    body["failed"] = failed;
    body["results"] = std::move(results);
    body["reloading"] = accepted;
    if (!accepted)
        body["error"] = (failed > 0) ? "no change was published — the batch is applied whole or not at all"
                                     : "nothing to commit";

    fill(resp, status, body.dump());
}

void SettingsController::reloadStatus(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)req;
    json body;

    const auto s = sm.reloadStatus();

    if (s == MgmtdServiceManager::ReloadStatus::Reloading)
        body["status"] = "reloading";
    else if (s == MgmtdServiceManager::ReloadStatus::Complete)
        body["status"] = "complete";
    else if (s == MgmtdServiceManager::ReloadStatus::Failed)
        body["status"] = "failed";
    else
        body["status"] = "idle";

    body["elapsed_ms"] = sm.reloadElapsedMs();

    fill(resp, 200, body.dump());
}

void SettingsController::commitQueue(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)req;
    fill(resp, 200, sm.commitQueueSnapshot());
}

namespace
{

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

}

void SettingsController::saveConfig(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    json input;
    if (!parseBody(req, resp, input))
        return;

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

void SettingsController::savedConfigs(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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
void SettingsController::savedConfigContent(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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


void SettingsController::onCommitStatus(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg)
{
    const auto& pl = msg.getPayload();
    if (pl.empty())
    {
        // An empty payload is not an empty queue — it is a message that lost its body. Overwriting a
        // good snapshot with "[]" would tell the browser every task finished.
        LOG_WARN("empty commit-queue snapshot — keeping the last one");
        return;
    }

    sm.setCommitQueue(std::string(pl.begin(), pl.end()));
}

}
