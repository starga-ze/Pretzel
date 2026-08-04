#include "service/topology/TopologyService.h"

#include "service/TopologydServiceManager.h"
#include "router/TopologydTxRouter.h"

#include "config/Config.h"
#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pz::topologyd
{

using json = nlohmann::json;

namespace
{

constexpr const char* kTs = "YYYY-MM-DD\"T\"HH24:MI:SSOF";

// What a collected sample is FOR. The classifier is deliberately the only place that maps an
// operator-defined endpoint onto a role in the picture, so when pretzel starts declaring its own
// feature APIs this is the single seam that changes — everything below consumes `Role`, not paths.
//
// Matching on the path is a guess about an endpoint someone else named, and it is the weakness of
// the current arrangement: rename the endpoint's path and the picture quietly loses a lane. It is
// what is available until the feature catalog lands.
enum class Role
{
    None,
    ZtnaGroups,       // ZTNA connector-groups
    ZtnaConnectors,   // ZTNA connectors
    NgfwInterfaces,   // ethernet interfaces
    NgfwTunnels,      // IPSec tunnels
};

bool contains(const std::string& hay, const char* needle)
{
    std::string h;
    h.reserve(hay.size());
    for (char c : hay)
        h += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string n;
    for (const char* p = needle; *p; ++p)
        n += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return h.find(n) != std::string::npos;
}

Role roleOf(const json& ep)
{
    const std::string deviceType = ep.value("device_type", std::string("ngfw"));
    const std::string path = ep.value("path", std::string());

    if (deviceType == "sase")
    {
        // Order matters: "connector-groups" also contains "connector".
        if (contains(path, "connector-groups"))
            return Role::ZtnaGroups;
        if (contains(path, "/connectors"))
            return Role::ZtnaConnectors;
        return Role::None;
    }

    if (contains(path, "IPSecTunnels") || contains(path, "ipsec"))
        return Role::NgfwTunnels;
    if (contains(path, "EthernetInterfaces") || contains(path, "ethernet"))
        return Role::NgfwInterfaces;
    return Role::None;
}

// PAN-OS REST wraps everything as {"result":{"entry":[…]}}; a single entry comes back as an object
// rather than a one-element array. The vendor's own JSON, normalised to a list once here so no
// caller has to remember which shape it got.
json entriesOf(const json& doc)
{
    if (!doc.is_object())
        return json::array();

    const json* node = &doc;
    if (doc.contains("result") && doc["result"].is_object())
        node = &doc["result"];

    if (!node->contains("entry"))
        return json::array();

    const json& e = (*node)["entry"];
    if (e.is_array())
        return e;
    if (e.is_object())
        return json::array({e});
    return json::array();
}

// The ZTNA API answers {"data":[…]} — a different envelope from PAN-OS, same idea.
json dataOf(const json& doc)
{
    if (doc.is_object() && doc.contains("data") && doc["data"].is_array())
        return doc["data"];
    return json::array();
}

std::string str(const json& o, const char* key)
{
    if (!o.is_object() || !o.contains(key) || o[key].is_null())
        return {};
    const auto& v = o[key];
    return v.is_string() ? v.get<std::string>() : v.dump();
}

// The addresses configured on an interface. PAN-OS nests them per mode (layer3/layer2/…), and each
// address is itself an `entry` whose NAME is the address — "10.0.0.1/24" is the key, not a value.
std::string interfaceIp(const json& entry)
{
    for (const char* mode : {"layer3", "layer2", "tap", "ha", "virtual-wire"})
    {
        if (!entry.contains(mode) || !entry[mode].is_object())
            continue;
        const json& m = entry[mode];
        if (!m.contains("ip"))
            continue;

        std::string out;
        for (const auto& a : entriesOf(m))
        {
            const std::string addr = str(a, "@name");
            if (addr.empty())
                continue;
            out += (out.empty() ? "" : ", ") + addr;
        }
        if (!out.empty())
            return out;

        // {"ip":{"entry":…}} handled above; some releases put a bare list under ip.
        if (m["ip"].is_object())
        {
            for (const auto& a : entriesOf(m["ip"]))
            {
                const std::string addr = str(a, "@name");
                if (addr.empty())
                    continue;
                out += (out.empty() ? "" : ", ") + addr;
            }
        }
        if (!out.empty())
            return out;
    }
    return {};
}

// Which mode an interface is configured in — the closest thing the config document has to "what is
// this interface for", and worth showing beside the address.
std::string interfaceMode(const json& entry)
{
    for (const char* mode : {"layer3", "layer2", "tap", "ha", "virtual-wire", "aggregate-group"})
        if (entry.contains(mode))
            return mode;
    return {};
}

// Admin state from the configuration. PAN-OS marks a shut interface with link-state=down (or, on
// some objects, `disabled`); anything else is administratively up.
//
// This is NOT operational state — the config API cannot say whether a cable is plugged in. The model
// therefore reports `admin_state` and the page says so, rather than printing "up" over a link that
// might be dark. Operational state needs `show interface`, which is a separate collection.
std::string adminState(const json& entry)
{
    if (entry.contains("link-state"))
    {
        const std::string ls = str(entry, "link-state");
        if (contains(ls, "down"))
            return "down";
        if (contains(ls, "up"))
            return "up";
    }
    const std::string disabled = str(entry, "disabled");
    if (disabled == "yes" || disabled == "true")
        return "down";
    return "up";
}

}

namespace
{
// Defined below, next to the other collection helpers; declared here so compose() can read the
// samples once before handing them to both halves.
SampleMap latestSamples();
}

void TopologyService::handleEvent(TopologydServiceManager& serviceManager, const TopologyEvent& event)
{
    if (event.type() != TopologyEventType::ReceiveRequest)
        return;

    const pz::ipc::IpcMessage* msg = event.message();
    if (!msg)
    {
        LOG_WARN("topology request without a message — dropping");
        return;
    }

    std::string siteOid;
    const auto& pl = msg->getPayload();
    if (!pl.empty())
    {
        try
        {
            siteOid = json::parse(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()))
                          .value("site", std::string());
        }
        catch (const std::exception& e)
        {
            LOG_WARN("topology request payload was not JSON ({}) — composing every site", e.what());
        }
    }

    json model;
    try
    {
        model = compose(siteOid);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("topology composition failed (site={}): {}", siteOid.empty() ? "all" : siteOid, e.what());
        model = json::object();
        model["error"] = "composition failed";
    }

    reply(serviceManager, msg->getSeqNo(), model);
}

nlohmann::json TopologyService::compose(const std::string& siteOid)
{
    json out;
    json sources = json::object();

    out["site"] = siteOid;
    out["sites"] = json::array();

    // When the composition ran, not when the data was collected — the page shows both, and
    // conflating them would flatter it. Each half carries its own collected_at.
    try
    {
        const auto rows = pz::db::Database::instance().queryRows(
            std::string("SELECT to_char(now(), '") + kTs + "')");
        out["generated_at"] = (!rows.empty() && !rows[0].empty()) ? rows[0][0] : std::string();
    }
    catch (const std::exception&)
    {
        out["generated_at"] = "";
    }

    const auto& site = pz::config::Config::serviceSection("engined", "site");
    for (const auto& s : site.value("sites", json::array()))
    {
        if (!s.is_object())
            continue;
        const std::string oid = s.value("oid", std::string());
        if (oid.empty())
            continue;
        out["sites"].push_back({{"oid", oid}, {"name", s.value("name", std::string())}});
    }

    // Read once, used by both halves.
    const SampleMap samples = latestSamples();
    out["sase"] = composeSase(siteOid, samples, sources);
    out["ngfw"] = composeNgfw(siteOid, samples, sources);
    out["sources"] = std::move(sources);

    LOG_DEBUG("topology composed (site={}, tenants={}, firewalls={})", siteOid.empty() ? "all" : siteOid,
              out["sase"]["tenants"].size(), out["ngfw"]["devices"].size());
    return out;
}

namespace
{

// Site name by oid, and the site an oid-less device belongs to (none).
std::unordered_map<std::string, std::string> siteNames()
{
    std::unordered_map<std::string, std::string> out;
    for (const auto& s : pz::config::Config::serviceSection("engined", "site").value("sites", json::array()))
    {
        if (!s.is_object())
            continue;
        const std::string oid = s.value("oid", std::string());
        if (!oid.empty())
            out[oid] = s.value("name", std::string());
    }
    return out;
}

// The newest sample body for every (connector, endpoint) stream, plus when it was collected. One
// query for the whole page: the alternative is a query per device, which on a large estate is the
// difference between one round trip and a hundred.
SampleMap latestSamples()
{
    SampleMap out;
    try
    {
        const auto rows = pz::db::Database::instance().queryRows(
            std::string("SELECT DISTINCT ON (connector_oid, endpoint_oid) connector_oid, endpoint_oid, "
                        "to_char(collected_at, '")
            + kTs
            + "'), ok::int, COALESCE(body,'') FROM api_collection "
              "ORDER BY connector_oid, endpoint_oid, collected_at DESC");

        for (const auto& r : rows)
        {
            if (r.size() < 5)
                continue;
            CollectedSample s;
            s.at = r[2];
            s.ok = (r[3] == "1");
            s.body = r[4];
            out[r[0] + '\x1f' + r[1]] = std::move(s);
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("could not read collection samples: {}", e.what());
    }
    return out;
}

// Every connector that collects from `deviceOid`, as (connector oid, endpoint oid, endpoint json).
struct Stream
{
    std::string connectorOid;
    std::string endpointOid;
    json endpoint;
};

std::vector<Stream> streamsForDevice(const std::string& deviceOid)
{
    std::vector<Stream> out;
    const auto& api = pz::config::Config::serviceSection("collectord", "api");

    std::unordered_map<std::string, json> endpoints;
    for (const auto& e : api.value("endpoints", json::array()))
    {
        if (!e.is_object())
            continue;
        const std::string oid = e.value("oid", std::string());
        if (!oid.empty())
            endpoints[oid] = e;
    }

    for (const auto& c : api.value("connectors", json::array()))
    {
        if (!c.is_object() || c.value("object", std::string()) != deviceOid)
            continue;
        const std::string connectorOid = c.value("oid", c.value("uuid", std::string()));
        for (const auto& i : c.value("items", json::array()))
        {
            if (!i.is_object() || i.value("enabled", true) == false)
                continue;
            const std::string endpointOid = i.value("endpoint", std::string());
            const auto ep = endpoints.find(endpointOid);
            if (endpointOid.empty() || ep == endpoints.end())
                continue;
            out.push_back(Stream{connectorOid, endpointOid, ep->second});
        }
    }
    return out;
}

json parseBody(const CollectedSample& s)
{
    if (!s.ok || s.body.empty())
        return json::object();
    try
    {
        return json::parse(s.body);
    }
    catch (const std::exception&)
    {
        return json::object();
    }
}

}

// The SASE half. Two sources, deliberately kept apart in the answer:
//
//   egress   sase_device.egress_result — the fabric itself (regions, gateways, egress addresses),
//            written by collectord's health probe. Passed through whole, as it is today: the drawing
//            is still being worked out and the page is the part that reloads without a rebuild.
//   ztna     the connector inventory, from collected samples. This is the private-application side
//            of the same tenant — the connectors that carry traffic on to the customer's own network.
nlohmann::json TopologyService::composeSase(const std::string& siteOid, const SampleMap& samples,
                                            nlohmann::json& sources)
{
    json out;
    out["tenants"] = json::array();

    const auto names = siteNames();
    int ztnaStreams = 0;

    std::unordered_map<std::string, json> deviceRows;
    try
    {
        const std::string sql =
            std::string("SELECT oid, COALESCE(name,''), COALESCE(site,''), COALESCE(target,''), "
                        "COALESCE(status,''), COALESCE(to_char(last_seen, '")
            + kTs + "'), ''), COALESCE(egress_result::text, '') FROM sase_device ORDER BY name";
        for (const auto& r : pz::db::Database::instance().queryRows(sql))
        {
            if (r.size() < 7 || r[0].empty())
                continue;
            deviceRows[r[0]] = json{{"oid", r[0]},   {"name", r[1]},      {"site", r[2]}, {"target", r[3]},
                                    {"status", r[4]}, {"last_seen", r[5]}, {"egress_raw", r[6]}};
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("could not read sase_device: {}", e.what());
    }

    // Config is the list of declared tenants; the table adds the runtime state. A tenant declared but
    // never probed still belongs in the picture — its absence is a finding, not a reason to hide it.
    const auto& site = pz::config::Config::serviceSection("engined", "site");
    for (const auto& d : site.value("sase_devices", json::array()))
    {
        if (!d.is_object())
            continue;
        const std::string oid = d.value("oid", std::string());
        const std::string devSite = d.value("site", std::string());
        if (oid.empty() || (!siteOid.empty() && devSite != siteOid))
            continue;

        json t;
        t["oid"] = oid;
        t["name"] = d.value("name", std::string());
        t["site"] = devSite;
        t["site_name"] = names.count(devSite) ? names.at(devSite) : std::string();
        t["target"] = d.value("target", std::string());

        const auto row = deviceRows.find(oid);
        t["status"] = row == deviceRows.end() ? "" : row->second.value("status", std::string());
        t["last_seen"] = row == deviceRows.end() ? "" : row->second.value("last_seen", std::string());

        t["egress"] = nullptr;
        if (row != deviceRows.end())
        {
            const std::string raw = row->second.value("egress_raw", std::string());
            if (!raw.empty())
            {
                try
                {
                    t["egress"] = json::parse(raw);
                }
                catch (const std::exception&)
                {
                    // A tenant that answered with something unparseable is the same as one that has
                    // not answered, as far as the drawing is concerned.
                }
            }
        }

        // ZTNA, from whatever this tenant's connectors collected.
        json ztna;
        ztna["groups"] = json::array();
        ztna["connectors"] = json::array();
        ztna["collected_at"] = "";

        for (const auto& s : streamsForDevice(oid))
        {
            const Role role = roleOf(s.endpoint);
            if (role != Role::ZtnaGroups && role != Role::ZtnaConnectors)
                continue;

            const auto it = samples.find(s.connectorOid + '\x1f' + s.endpointOid);
            if (it == samples.end())
                continue;
            ++ztnaStreams;

            const json doc = parseBody(it->second);
            const json rows = dataOf(doc);
            if (role == Role::ZtnaGroups)
                ztna["groups"] = rows;
            else
                ztna["connectors"] = rows;

            // The tenant's ZTNA view is as old as its oldest contributing sample.
            const std::string at = it->second.at;
            const std::string cur = ztna["collected_at"].get<std::string>();
            if (cur.empty() || (!at.empty() && at < cur))
                ztna["collected_at"] = at;
        }

        // Group name by oid, so a connector can say which group it serves without the page joining.
        json groupNames = json::object();
        for (const auto& g : ztna["groups"])
            if (g.is_object() && !str(g, "oid").empty())
                groupNames[str(g, "oid")] = str(g, "name");
        ztna["group_names"] = std::move(groupNames);

        t["ztna"] = std::move(ztna);
        out["tenants"].push_back(std::move(t));
    }

    sources["sase_tenants"] = out["tenants"].size();
    sources["ztna_streams"] = ztnaStreams;
    return out;
}

// The NGFW half: the customer's own firewalls, drawn from two collected documents — the ethernet
// interfaces and the IPSec tunnels.
//
// Note on what is NOT drawn here: a ZTNA connector is not an NGFW peer. The connectors sit on VMs
// behind the firewall and tunnel to the Prisma fabric directly, so correlating an NGFW tunnel's peer
// against a connector's public address would invent a link that does not exist. The two halves of
// this answer are two estates, not two ends of one wire.
nlohmann::json TopologyService::composeNgfw(const std::string& siteOid, const SampleMap& samples,
                                            nlohmann::json& sources)
{
    json out;
    out["devices"] = json::array();

    const auto names = siteNames();
    int ifStreams = 0;
    int tunStreams = 0;

    std::unordered_map<std::string, json> deviceRows;
    try
    {
        const std::string sql =
            std::string("SELECT oid, COALESCE(status,''), COALESCE(to_char(last_seen, '") + kTs +
            "'), '') FROM ngfw_device";
        for (const auto& r : pz::db::Database::instance().queryRows(sql))
            if (r.size() >= 3 && !r[0].empty())
                deviceRows[r[0]] = json{{"status", r[1]}, {"last_seen", r[2]}};
    }
    catch (const std::exception& e)
    {
        LOG_WARN("could not read ngfw_device: {}", e.what());
    }

    const auto& site = pz::config::Config::serviceSection("engined", "site");
    for (const auto& d : site.value("ngfw_devices", json::array()))
    {
        if (!d.is_object())
            continue;
        const std::string oid = d.value("oid", std::string());
        const std::string devSite = d.value("site", std::string());
        if (oid.empty() || (!siteOid.empty() && devSite != siteOid))
            continue;

        json fw;
        fw["oid"] = oid;
        fw["name"] = d.value("name", std::string());
        fw["site"] = devSite;
        fw["site_name"] = names.count(devSite) ? names.at(devSite) : std::string();
        fw["target"] = d.value("target", std::string());

        const auto row = deviceRows.find(oid);
        fw["status"] = row == deviceRows.end() ? "" : row->second.value("status", std::string());
        fw["last_seen"] = row == deviceRows.end() ? "" : row->second.value("last_seen", std::string());

        json interfaces = {{"list", json::array()}, {"total", 0}, {"up", 0}, {"down", 0},
                           {"with_ip", 0},          {"collected_at", ""},    {"collected", false}};
        json tunnels = {{"list", json::array()}, {"total", 0}, {"enabled", 0}, {"disabled", 0},
                        {"collected_at", ""},    {"collected", false}};

        for (const auto& s : streamsForDevice(oid))
        {
            const Role role = roleOf(s.endpoint);
            if (role != Role::NgfwInterfaces && role != Role::NgfwTunnels)
                continue;

            const auto it = samples.find(s.connectorOid + '\x1f' + s.endpointOid);
            if (it == samples.end())
                continue;

            const json entries = entriesOf(parseBody(it->second));

            if (role == Role::NgfwInterfaces)
            {
                ++ifStreams;
                interfaces["collected"] = true;
                interfaces["collected_at"] = it->second.at;

                int up = 0, down = 0, withIp = 0;
                for (const auto& e : entries)
                {
                    if (!e.is_object())
                        continue;
                    const std::string ip = interfaceIp(e);
                    const std::string state = adminState(e);
                    state == "down" ? ++down : ++up;
                    if (!ip.empty())
                        ++withIp;

                    interfaces["list"].push_back({{"name", str(e, "@name")},
                                                  {"ip", ip},
                                                  {"mode", interfaceMode(e)},
                                                  {"comment", str(e, "comment")},
                                                  {"admin_state", state}});
                }
                interfaces["total"] = interfaces["list"].size();
                interfaces["up"] = up;
                interfaces["down"] = down;
                interfaces["with_ip"] = withIp;
            }
            else
            {
                ++tunStreams;
                tunnels["collected"] = true;
                tunnels["collected_at"] = it->second.at;

                int enabled = 0, disabled = 0;
                for (const auto& e : entries)
                {
                    if (!e.is_object())
                        continue;

                    // The peer address lives on the IKE gateway, which is a separate object this
                    // does not collect — so the gateway is named and the peer is left honestly blank
                    // rather than guessed at.
                    std::string gateway;
                    std::string crypto;
                    if (e.contains("auto-key") && e["auto-key"].is_object())
                    {
                        const json& ak = e["auto-key"];
                        crypto = str(ak, "ipsec-crypto-profile");
                        if (ak.contains("ike-gateway"))
                        {
                            const json gws = entriesOf(ak["ike-gateway"]);
                            for (const auto& g : gws)
                            {
                                const std::string n = str(g, "@name");
                                if (n.empty())
                                    continue;
                                gateway += (gateway.empty() ? "" : ", ") + n;
                            }
                        }
                    }

                    const std::string dis = str(e, "disabled");
                    const bool off = (dis == "yes" || dis == "true");
                    off ? ++disabled : ++enabled;

                    std::string monitor;
                    if (e.contains("tunnel-monitor") && e["tunnel-monitor"].is_object())
                        monitor = str(e["tunnel-monitor"], "enable");

                    tunnels["list"].push_back({{"name", str(e, "@name")},
                                               {"interface", str(e, "tunnel-interface")},
                                               {"gateway", gateway},
                                               {"crypto", crypto},
                                               {"monitor", monitor},
                                               {"enabled", !off}});
                }
                tunnels["total"] = tunnels["list"].size();
                tunnels["enabled"] = enabled;
                tunnels["disabled"] = disabled;
            }
        }

        fw["interfaces"] = std::move(interfaces);
        fw["tunnels"] = std::move(tunnels);
        out["devices"].push_back(std::move(fw));
    }

    sources["ngfw_devices"] = out["devices"].size();
    sources["interface_streams"] = ifStreams;
    sources["tunnel_streams"] = tunStreams;
    return out;
}

void TopologyService::reply(TopologydServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& model)
{
    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Topologyd);
    msg->setDst(pz::ipc::IpcDaemon::Mgmtd);
    msg->setCmd(pz::ipc::IpcCmd::TopologyResponse);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response));
    msg->setSeqNo(seqNo);

    const std::string body = model.dump();
    msg->setPayload(std::vector<std::uint8_t>(body.begin(), body.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

}
