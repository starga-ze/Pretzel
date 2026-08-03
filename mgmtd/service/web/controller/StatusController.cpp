#include "service/web/controller/StatusController.h"

#include "service/web/WebUtil.h"

#include "config/Config.h"
#include "db/Database.h"
#include "http/HttpMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

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

// The material the Site Topology page draws, in one fetch for one picture:
//
//   tenants[]  each SASE tenant plus the last getPrismaAccessIP document engined cached for it
//              (sase_device.egress_result — zones, MU-SPN / portal node addresses, proxy FQDNs)
//   ngfw[]     the on-premise firewalls we manage
//
// The NGFW inventory belongs in the same answer because of where Prisma Access ends: a Service
// Connection terminates on an SC-CAN, and an SC-CAN performs no inspection — the enforcement point
// for private-app traffic is the customer's own firewall at the far end. Drawing the fabric without
// it stops the picture one hop short of where policy actually applies.
//
// The tenant document is passed through whole rather than reduced to a graph here: the shape of the
// drawing is still being worked out, and the page is the part that reloads without a rebuild. When
// the IPsec / routing / ZTNA-connector reads land they join this response as further sibling keys.
void StatusController::siteTopology(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    (void)req;

    json out;
    out["tenants"] = json::array();
    out["ngfw"] = json::array();

    try
    {
        auto& db = pz::db::Database::instance();

        // Site names come from config, not the database: a site is operator-declared and the device
        // row carries only its oid.
        std::unordered_map<std::string, std::string> siteName;
        const auto& site = pz::config::Config::serviceSection("engined", "site");
        for (const auto& s : site.value("sites", json::array()))
        {
            if (!s.is_object())
                continue;
            const std::string oid = s.value("oid", std::string());
            if (!oid.empty())
                siteName[oid] = s.value("name", std::string());
        }

        constexpr const char* kTs = "YYYY-MM-DD\"T\"HH24:MI:SSOF";
        const std::string sql =
            std::string("SELECT oid, COALESCE(name,''), COALESCE(site,''), COALESCE(target,''), ")
            + "COALESCE(status,''), COALESCE(to_char(last_seen, '" + kTs + "'), ''), "
            + "COALESCE(to_char(updated_at, '" + kTs + "'), ''), COALESCE(egress_result::text, '') "
            + "FROM sase_device ORDER BY name";

        for (const auto& r : db.queryRows(sql))
        {
            if (r.size() < 8 || r[0].empty())
                continue;

            json t;
            t["oid"] = r[0];
            t["name"] = r[1];
            t["site"] = r[2];
            t["site_name"] = siteName.count(r[2]) ? siteName[r[2]] : std::string();
            t["target"] = r[3];
            t["status"] = r[4];
            t["last_seen"] = r[5];
            t["updated_at"] = r[6];

            // A tenant that has never answered has no document yet; say so with null rather than an
            // empty object, so the page can tell "not probed" from "answered with nothing".
            auto doc = json::parse(r[7], nullptr, false);
            t["egress"] = doc.is_discarded() ? json(nullptr) : std::move(doc);

            out["tenants"].push_back(std::move(t));
        }

        // The private side of the picture. Only what the operator declared and engined probes — no
        // attempt to guess which firewall sits behind which Service Connection, because nothing in
        // the estate knows that yet.
        for (const auto& r : db.queryRows(
                 "SELECT oid, COALESCE(name,''), COALESCE(site,''), COALESCE(target,''), "
                 "COALESCE(status,'') FROM ngfw_device ORDER BY name"))
        {
            if (r.size() < 5 || r[0].empty())
                continue;
            out["ngfw"].push_back({{"oid", r[0]},
                                   {"name", r[1]},
                                   {"site", r[2]},
                                   {"site_name", siteName.count(r[2]) ? siteName[r[2]] : std::string()},
                                   {"target", r[3]},
                                   {"status", r[4]}});
        }

        const auto now = db.queryRows("SELECT to_char(now(), '" + std::string(kTs) + "')");
        out["generated_at"] = (!now.empty() && !now[0].empty()) ? now[0][0] : std::string();
    }
    catch (const std::exception& e)
    {
        LOG_WARN("site topology query failed: {}", e.what());
    }

    fill(resp, 200, out.dump());
}

}
