#include "service/web/controller/StatusController.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

#include "config/Config.h"
#include "db/Database.h"
#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pz::mgmtd
{

using json = nlohmann::json;

// Every managed device is judged on the same three-layer dependency ladder, regardless of type:
//   reachable  — can we reach it at all      (NGFW: ICMP · SASE: getPrismaAccessIP control-plane)
//   credential — do we hold valid access     (the device's bound API Key / OAuth token)
//   api        — does a real API call answer  (the device's connector's last collection sample)
// Each layer is "ok" / "fail" / "unknown" (unknown = nothing configured for that layer yet). engined
// owns devices.status (reachable); the credential and api layers are derived here by joining the
// operator's config (which API Key / connector each device carries) with the runtime state tables.
void StatusController::deviceStatus(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    (void)req;

    json out = json::object();
    try
    {
        auto& db = pz::db::Database::instance();

        // credential state, keyed by API Key (auth_profile) oid.
        std::unordered_map<std::string, bool> credOk;   // oid -> healthy (stored + last test ok + unexpired)
        std::unordered_map<std::string, bool> credSeen;  // oid -> a row exists
        for (const auto& r : db.queryRows(
                 "SELECT oid, (key_enc IS NOT NULL AND last_test_ok "
                 "AND (expires_at IS NULL OR expires_at > now()))::int FROM api_credential_state"))
        {
            if (r.size() < 2 || r[0].empty())
                continue;
            credSeen[r[0]] = true;
            credOk[r[0]] = (r[1] == "1");
        }

        // latest collection outcome per connector.
        std::unordered_map<std::string, bool> apiOk;
        for (const auto& r : db.queryRows(
                 "SELECT DISTINCT ON (connector_oid) connector_oid, ok::int "
                 "FROM api_collection ORDER BY connector_oid, collected_at DESC"))
        {
            if (r.size() < 2 || r[0].empty())
                continue;
            apiOk[r[0]] = (r[1] == "1");
        }

        // device reachability from engined's projection.
        std::unordered_map<std::string, std::string> reachable;   // device oid -> status
        for (const auto& r : db.queryRows("SELECT oid, COALESCE(status,'') FROM ngfw_device "
                                          "UNION ALL SELECT oid, COALESCE(status,'') FROM sase_device"))
        {
            if (!r.empty() && !r[0].empty())
                reachable[r[0]] = r.size() > 1 ? r[1] : std::string();
        }

        // config: which API Key / connector each device carries.
        const auto& api = pz::config::Config::serviceSection("collectord", "api");
        const auto& site = pz::config::Config::serviceSection("engined", "site");

        // device oid -> best credential layer across its bound API Keys.
        std::unordered_map<std::string, std::string> credLayer;
        for (const auto& p : api.value("api_credentials", json::array()))
        {
            if (!p.is_object())
                continue;
            const std::string dev = p.value("device", std::string());
            const std::string oid = p.value("oid", std::string());
            if (dev.empty() || oid.empty())
                continue;
            const bool seen = credSeen.count(oid) != 0;
            const std::string layer = !seen ? "unknown" : (credOk[oid] ? "ok" : "fail");
            auto& cur = credLayer[dev];
            // ok wins over fail wins over unknown.
            if (cur.empty() || layer == "ok" || (layer == "fail" && cur == "unknown"))
                cur = layer;
        }

        // device oid -> best api layer across its connectors.
        std::unordered_map<std::string, std::string> apiLayer;
        for (const auto& c : api.value("connectors", json::array()))
        {
            if (!c.is_object())
                continue;
            const std::string dev = c.value("object", std::string());
            const std::string oid = c.value("oid", std::string());
            if (dev.empty() || oid.empty())
                continue;
            const bool seen = apiOk.count(oid) != 0;
            const std::string layer = !seen ? "unknown" : (apiOk[oid] ? "ok" : "fail");
            auto& cur = apiLayer[dev];
            if (cur.empty() || layer == "ok" || (layer == "fail" && cur == "unknown"))
                cur = layer;
        }

        json devices = json::array();
        for (const char* key : {"ngfw_devices", "sase_devices"})
            for (const auto& d : site.value(key, json::array()))
                devices.push_back(d);

        for (const auto& d : devices)
        {
            if (!d.is_object())
                continue;
            const std::string oid = d.value("oid", std::string());
            if (oid.empty())
                continue;

            const std::string st = reachable.count(oid) ? reachable[oid] : std::string();
            const std::string reach = (st == "active") ? "ok" : (st == "down" ? "fail" : "unknown");
            const std::string cred = credLayer.count(oid) ? credLayer[oid] : "unknown";
            const std::string apil = apiLayer.count(oid) ? apiLayer[oid] : "unknown";

            out[oid] = {{"reachable", reach}, {"credential", cred}, {"api", apil}};
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("device status query failed: {}", e.what());
    }

    fill(resp, 200, out.dump());
}
// GET /api/topology?site=<oid>
//
// mgmtd owns no topology logic: it asks topologyd to compose the site and serves what came back.
// The HTTP response is never held open for that round trip — responses here are built synchronously
// on the loop every other daemon's messages arrive on, so waiting for one browser would stall every
// other daemon's traffic behind it.
//
// Instead the answer carries `pending`: "a fresher composition is on its way". The page re-asks in a
// moment and draws that. A round trip is 10-30ms (the database reads measure ~4ms; the rest is two
// IPC hops), so the picture is never meaningfully behind.
//
// What is deliberately NOT done: blanking the picture while refreshing. A composition that lands in
// 20ms would turn every periodic refresh into a flicker. `pending` and "there is nothing to draw"
// are therefore separate facts, and the page shows its composing state only when it has neither.
void StatusController::siteTopology(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                    pz::http::HttpResponse& resp)
{
    const std::string siteOid = queryParam(req.target, "site");

    // Ask only when the answer would be new: nothing cached, or what is cached has aged past the
    // page's own refresh interval. One outstanding request per site — a burst of polls (several tabs,
    // a held-down refresh) must not become a burst of compositions.
    constexpr auto kFreshFor = std::chrono::seconds(50);
    const bool haveFresh = sm.topologyFresh(siteOid, kFreshFor);

    if (!haveFresh && !sm.topologyRequested(siteOid))
    {
        json ask;
        ask["site"] = siteOid;
        const std::string body = ask.dump();

        auto msg = std::make_unique<pz::ipc::IpcMessage>();
        msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
        msg->setDst(pz::ipc::IpcDaemon::Topologyd);
        msg->setCmd(pz::ipc::IpcCmd::TopologyRequest);
        msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
        msg->setPayload(std::vector<std::uint8_t>(body.begin(), body.end()));

        sm.txRouter().handleIpcMessage(std::move(msg));
        sm.markTopologyRequested(siteOid);
    }

    if (const std::string* model = sm.topology(siteOid))
    {
        // Spliced in rather than re-serialising: the model is already a JSON string and can be tens
        // of kilobytes, and this runs on the request path.
        std::string out = *model;
        const auto brace = out.find('{');
        if (brace != std::string::npos)
            out.insert(brace + 1, haveFresh ? "\"pending\":false," : "\"pending\":true,");
        return fill(resp, 200, out);
    }

    // Nothing composed for this site yet. A well-formed empty model rather than an error: the page
    // renders its composing state from `pending` and never has to special-case a 503.
    json out;
    out["site"] = siteOid;
    out["pending"] = true;
    out["sites"] = json::array();
    out["sase"] = {{"tenants", json::array()}};
    out["ngfw"] = {{"devices", json::array()}};
    out["sources"] = json::object();
    fill(resp, 200, out.dump());
}

}
