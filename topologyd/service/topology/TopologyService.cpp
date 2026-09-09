#include "service/topology/TopologyService.h"

#include "service/TopologydServiceManager.h"
#include "service/topology/NgfwModel.h"
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
    ZtnaGroups,        // ZTNA connector-groups
    ZtnaConnectors,    // ZTNA connectors
    SaseServiceConns,  // Service Connections — the tenant's data-centre hand-off
    SaseIpsec,         // SCM IPSec tunnels — the middle link between a connection and its peer
    SaseIke,           // SCM IKE gateways — where the Prisma side records OUR firewall's address
    NgfwInterfaces,    // ethernet interfaces
    NgfwTunnels,       // IPSec tunnels
    NgfwIke,           // IKE gateways — where the peer address lives, and so every off-box link
    NgfwTunnelIfs,     // tunnel interface units
    NgfwGpPortals,     // GlobalProtect portals
    NgfwGpGateways,    // GlobalProtect gateways
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
        // Order matters: "connector-groups" also contains "connector", and "service-connections" is
        // a different product's noun that reads like both. Most specific first, as below.
        if (contains(path, "service-connections"))
            return Role::SaseServiceConns;
        // Both of these live under /config/network/v1 and are read with folder=Service Connections.
        // They were being collected and thrown away: the deployment read answers eight fields and a
        // peer address is not among them, so without these two a Service Connection could say where
        // it lands but not what it dials. Most specific first — "ike-gateways" also contains
        // "gateway", and an ipsec-tunnels path contains "tunnel".
        if (contains(path, "ike-gateways"))
            return Role::SaseIke;
        if (contains(path, "ipsec-tunnels"))
            return Role::SaseIpsec;
        if (contains(path, "connector-groups"))
            return Role::ZtnaGroups;
        if (contains(path, "/connectors"))
            return Role::ZtnaConnectors;
        return Role::None;
    }

    // Order matters throughout: "IKEGatewayNetworkProfiles" contains "gateway", and
    // "TunnelInterfaces" contains both "tunnel" and "interface". The most specific token wins, and
    // the loose fallbacks stay last so an operator who renamed a path still lands somewhere sane.
    if (contains(path, "GlobalProtectPortals"))
        return Role::NgfwGpPortals;
    if (contains(path, "GlobalProtectGateways"))
        return Role::NgfwGpGateways;
    if (contains(path, "IKEGateway") || contains(path, "ike-gateway"))
        return Role::NgfwIke;
    if (contains(path, "TunnelInterfaces"))
        return Role::NgfwTunnelIfs;
    if (contains(path, "IPSecTunnels") || contains(path, "ipsec"))
        return Role::NgfwTunnels;
    if (contains(path, "EthernetInterfaces") || contains(path, "ethernet"))
        return Role::NgfwInterfaces;
    return Role::None;
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

    const auto& site = pz::config::Config::section(pz::config::scope::kPretzel, "site");
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
    linkServiceConnections(out, sources);
    out["sources"] = std::move(sources);

    LOG_TRACE("topology composed (site={}, tenants={}, firewalls={})", siteOid.empty() ? "all" : siteOid,
              out["sase"]["tenants"].size(), out["ngfw"]["devices"].size());
    return out;
}

namespace
{

// Site name by oid, and the site an oid-less device belongs to (none).
std::unordered_map<std::string, std::string> siteNames()
{
    std::unordered_map<std::string, std::string> out;
    for (const auto& s : pz::config::Config::section(pz::config::scope::kPretzel, "site").value("sites", json::array()))
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
    const auto& api = pz::config::Config::section(pz::config::scope::kPretzel, "connector");

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
    int scStreams = 0;

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
    const auto& site = pz::config::Config::section(pz::config::scope::kPretzel, "site");
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

        // The two ways a tenant reaches the customer's own estate, from whatever it collected. They
        // are independent — a tenant may run Service Connections, ZTNA connectors, or both — so each
        // carries its own freshness and each is drawn from its own streams.
        json ztna;
        ztna["groups"] = json::array();
        ztna["connectors"] = json::array();
        ztna["collected_at"] = "";

        json sc;
        sc["connections"] = json::array();
        sc["collected_at"] = "";

        // The two documents that stand between a Service Connection and the address it dials. Held
        // aside rather than published: they are a lookup, not a lane, and what the picture wants is
        // the answer they resolve to.
        json scmTunnels = json::array();
        json scmGateways = json::array();

        for (const auto& s : streamsForDevice(oid))
        {
            const Role role = roleOf(s.endpoint);
            if (role != Role::ZtnaGroups && role != Role::ZtnaConnectors &&
                role != Role::SaseServiceConns && role != Role::SaseIpsec && role != Role::SaseIke)
                continue;

            const auto it = samples.find(s.connectorOid + '\x1f' + s.endpointOid);
            if (it == samples.end())
                continue;

            const json rows = dataOf(parseBody(it->second));
            const std::string at = it->second.at;

            // A lane's view is as old as its oldest contributing sample.
            const auto age = [&at](json& lane) {
                const std::string cur = lane["collected_at"].get<std::string>();
                if (cur.empty() || (!at.empty() && at < cur))
                    lane["collected_at"] = at;
            };

            if (role == Role::SaseIpsec || role == Role::SaseIke)
            {
                // Same "fuller answer wins" rule as the connections themselves: a tenant may carry
                // both a list read and a single-object read of the same resource.
                json& into = (role == Role::SaseIpsec) ? scmTunnels : scmGateways;
                if (rows.size() >= into.size())
                    into = rows;
                age(sc);
                continue;
            }

            if (role == Role::SaseServiceConns)
            {
                ++scStreams;
                // One tenant can carry both a list read and a single-object read of the same
                // resource — they are two endpoints on one path, and both classify here. The fuller
                // answer wins rather than the last one seen, so a `.../<id>` stream cannot shrink
                // the lane to the one connection it asked about.
                if (rows.size() >= sc["connections"].size())
                    sc["connections"] = rows;
                age(sc);
                continue;
            }

            ++ztnaStreams;
            if (role == Role::ZtnaGroups)
                ztna["groups"] = rows;
            else
                ztna["connectors"] = rows;
            age(ztna);
        }

        // ── The peer address, resolved ────────────────────────────────────────────────────────
        //
        // A Service Connection names an `ipsec_tunnel`; the tunnel names an `ike_gateway`; the
        // gateway records a `peer_address`. Three documents, two joins, and the answer at the end is
        // the single most useful fact about the connection — because on the Prisma side the peer is
        // OUR firewall. The customer's own box, named by the service, in the service's own config.
        //
        // Resolved here rather than on the page because it is a join over three vendor documents and
        // the page should be handed answers, not homework.
        {
            // tunnel name → the gateway it uses. `auto_key.ike_gateway` is a list even when there is
            // one, and SCM collapses one-element lists to an object often enough that both shapes
            // have to be read.
            std::unordered_map<std::string, std::string> gwOfTunnel;
            for (const auto& t : scmTunnels)
            {
                if (!t.is_object())
                    continue;
                const std::string name = str(t, "name");
                if (name.empty() || !t.contains("auto_key") || !t["auto_key"].is_object())
                    continue;
                const auto& gw = t["auto_key"]["ike_gateway"];
                if (gw.is_array() && !gw.empty() && gw[0].is_object())
                    gwOfTunnel[name] = str(gw[0], "name");
                else if (gw.is_object())
                    gwOfTunnel[name] = str(gw, "name");
            }

            // gateway name → what it dials. `peer_address` is one of three shapes and they are not
            // interchangeable: an address can be matched against a firewall's interfaces, an FQDN
            // cannot, and `dynamic` means the far end has no fixed address at all. Each is recorded
            // as what it is rather than flattened to a string, so nothing downstream has to guess
            // which kind it got.
            struct GwPeer { std::string ip, fqdn; bool dynamic{false}; };
            std::unordered_map<std::string, GwPeer> peerOfGw;
            for (const auto& g : scmGateways)
            {
                if (!g.is_object())
                    continue;
                const std::string name = str(g, "name");
                if (name.empty() || !g.contains("peer_address") || !g["peer_address"].is_object())
                    continue;
                const auto& pa = g["peer_address"];
                GwPeer p;
                p.ip = str(pa, "ip");
                p.fqdn = str(pa, "fqdn");
                p.dynamic = pa.contains("dynamic");
                peerOfGw[name] = std::move(p);
            }

            for (auto& c : sc["connections"])
            {
                if (!c.is_object())
                    continue;
                const std::string tun = str(c, "ipsec_tunnel");
                const auto gi = gwOfTunnel.find(tun);
                const std::string gw = (gi == gwOfTunnel.end()) ? std::string() : gi->second;
                c["ike_gateway"] = gw;

                const auto pi = peerOfGw.find(gw);
                c["peer_ip"] = (pi == peerOfGw.end()) ? std::string() : pi->second.ip;
                c["peer_fqdn"] = (pi == peerOfGw.end()) ? std::string() : pi->second.fqdn;
                c["peer_dynamic"] = (pi != peerOfGw.end()) && pi->second.dynamic;
                // Filled by linkServiceConnections() once the firewalls are composed; declared here
                // so a connection's shape does not depend on whether the join found anything.
                c["linked"] = false;
                c["linked_device"] = "";
                c["linked_device_name"] = "";
                c["linked_interface"] = "";
            }
        }

        // Group name by oid, so a connector can say which group it serves without the page joining.
        json groupNames = json::object();
        for (const auto& g : ztna["groups"])
            if (g.is_object() && !str(g, "oid").empty())
                groupNames[str(g, "oid")] = str(g, "name");
        ztna["group_names"] = std::move(groupNames);

        t["ztna"] = std::move(ztna);
        t["sc"] = std::move(sc);
        out["tenants"].push_back(std::move(t));
    }

    sources["sase_tenants"] = out["tenants"].size();
    sources["ztna_streams"] = ztnaStreams;
    sources["sc_streams"] = scStreams;
    return out;
}

// The NGFW half: the customer's own firewalls, and — new here — how they reach the fabric.
//
// The old version drew two documents (interfaces, IPSec tunnels) and stopped at the box. It could
// not draw a link, because an IPSec tunnel names an IKE gateway and the peer address lives on the
// gateway, which was not collected. With the gateway document in hand, every off-box relationship
// the firewall has becomes readable, and that is what turns a grid of cards into a topology.
//
// Three things are produced:
//
//   devices   what each firewall IS — interfaces with a role (which side is the internet), the
//             tunnels, the IKE gateways, the GlobalProtect portals and gateways.
//   links     one edge per IKE gateway that has a peer. This is the traffic-bearing relationship:
//             firewall, out of this interface, to that Service Connection / Remote Network / peer.
//   shape     whether the fabric or a firewall is the hub — derived, not configured. See below.
//
// What is deliberately NOT correlated: a ZTNA connector is not an NGFW peer. Connectors sit on VMs
// behind the firewall and tunnel to Prisma directly, so matching a tunnel's peer against a
// connector's address would invent a link that does not exist.
nlohmann::json TopologyService::composeNgfw(const std::string& siteOid, const SampleMap& samples,
                                            nlohmann::json& sources)
{
    namespace nm = pz::topologyd::ngfw;

    json out;
    out["devices"] = json::array();
    out["links"] = json::array();

    const auto names = siteNames();
    int ifStreams = 0;
    int tunStreams = 0;
    int ikeStreams = 0;
    int gpStreams = 0;

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

    const auto& site = pz::config::Config::section(pz::config::scope::kPretzel, "site");
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
                           {"with_ip", 0},          {"wan", 0},   {"edge", 0},  {"lan", 0},
                           {"collected_at", ""},    {"collected", false}};
        json tunnels = {{"list", json::array()}, {"total", 0}, {"enabled", 0}, {"disabled", 0},
                        {"collected_at", ""},    {"collected", false}};
        json ike = {{"list", json::array()}, {"total", 0}, {"collected_at", ""}, {"collected", false}};
        json gp = {{"portals", json::array()}, {"gateways", json::array()},
                   {"collected_at", ""},       {"collected", false}};

        // The raw interface document is needed twice — once to list the interfaces, once to decide
        // which of them are the internet edge, and the second pass needs the IKE and GP documents
        // that may arrive from a different stream. So the documents are gathered first and the
        // model is built afterwards, rather than built as the streams are walked.
        json ifDoc, tunDoc, ikeDoc, tunIfDoc, portalDoc, gwDoc;

        for (const auto& s : streamsForDevice(oid))
        {
            const Role role = roleOf(s.endpoint);
            if (role == Role::None || role == Role::ZtnaGroups || role == Role::ZtnaConnectors)
                continue;

            const auto it = samples.find(s.connectorOid + '\x1f' + s.endpointOid);
            if (it == samples.end())
                continue;

            const json doc = parseBody(it->second);
            const std::string at = it->second.at;

            switch (role)
            {
            case Role::NgfwInterfaces:
                ifDoc = doc;
                ++ifStreams;
                interfaces["collected"] = true;
                interfaces["collected_at"] = at;
                break;
            case Role::NgfwTunnels:
                tunDoc = doc;
                ++tunStreams;
                tunnels["collected"] = true;
                tunnels["collected_at"] = at;
                break;
            case Role::NgfwIke:
                ikeDoc = doc;
                ++ikeStreams;
                ike["collected"] = true;
                ike["collected_at"] = at;
                break;
            case Role::NgfwTunnelIfs:
                tunIfDoc = doc;
                break;
            case Role::NgfwGpPortals:
                portalDoc = doc;
                ++gpStreams;
                gp["collected"] = true;
                gp["collected_at"] = at;
                break;
            case Role::NgfwGpGateways:
                gwDoc = doc;
                ++gpStreams;
                gp["collected"] = true;
                if (gp["collected_at"].get<std::string>().empty())
                    gp["collected_at"] = at;
                break;
            default:
                break;
            }
        }

        // ── IKE gateways: read first, because the interface roles depend on them ──────────────
        const auto ikeGws = nm::readIkeGateways(ikeDoc);
        const auto portals = nm::readGpPortals(portalDoc);
        const auto gateways = nm::readGpGateways(gwDoc);

        // Which interfaces something terminates on. An interface that carries a VPN or fronts
        // GlobalProtect is an edge whatever its address says — this is the configuration stating a
        // fact, where the address is only ever an inference.
        std::unordered_map<std::string, int> vpnOn;   // interface -> IKE gateways terminating there
        std::unordered_map<std::string, int> gpOn;    // interface -> GP portals/gateways there
        for (const auto& g : ikeGws)
            if (!g.interfaceName.empty())
                ++vpnOn[g.interfaceName];
        for (const auto& p : portals)
            if (!p.interfaceName.empty())
                ++gpOn[p.interfaceName];
        for (const auto& g : gateways)
            if (!g.interfaceName.empty())
                ++gpOn[g.interfaceName];

        // ── Interfaces ────────────────────────────────────────────────────────────────────────
        {
            int up = 0, down = 0, withIp = 0, wan = 0, edge = 0, lan = 0;
            for (const auto& e : nm::entriesOf(ifDoc))
            {
                if (!e.is_object())
                    continue;

                const std::string name = nm::str(e, "@name");
                const auto addrs = nm::interfaceAddresses(e);
                const std::string state = nm::adminState(e);
                state == "down" ? ++down : ++up;
                if (!addrs.empty())
                    ++withIp;

                // The role. A public address is the internet edge outright; a private one that
                // nonetheless terminates a VPN or GlobalProtect is `edge` — see IfRole. An
                // interface with no address at all is left unknown rather than filed as inside,
                // because a spare port and a LAN port are not the same finding.
                const bool terminates = vpnOn.count(name) || gpOn.count(name);
                nm::IfRole role = nm::IfRole::Unknown;
                if (name.rfind("loopback", 0) == 0)
                    role = nm::IfRole::Loopback;
                else if (name.rfind("tunnel", 0) == 0)
                    role = nm::IfRole::Tunnel;
                else if (!addrs.empty())
                {
                    bool anyPublic = false;
                    for (const auto& a : addrs)
                        if (!nm::isPrivateV4(a))
                            anyPublic = true;
                    role = anyPublic ? nm::IfRole::Wan
                                     : (terminates ? nm::IfRole::Edge : nm::IfRole::Lan);
                }
                else if (terminates)
                {
                    // Services bound to an interface the document gives no address for. It is
                    // still where the outside arrives, which is the whole point of the role.
                    role = nm::IfRole::Edge;
                }

                if (role == nm::IfRole::Wan)
                    ++wan;
                else if (role == nm::IfRole::Edge)
                    ++edge;
                else if (role == nm::IfRole::Lan)
                    ++lan;

                std::string joined;
                for (const auto& a : addrs)
                    joined += (joined.empty() ? "" : ", ") + a;

                interfaces["list"].push_back({{"name", name},
                                              {"ip", joined},
                                              {"addresses", addrs},
                                              {"mode", nm::interfaceMode(e)},
                                              {"comment", nm::str(e, "comment")},
                                              {"role", nm::ifRoleName(role)},
                                              {"vpn_count", vpnOn.count(name) ? vpnOn.at(name) : 0},
                                              {"gp_count", gpOn.count(name) ? gpOn.at(name) : 0},
                                              {"admin_state", state}});
            }
            interfaces["total"] = interfaces["list"].size();
            interfaces["up"] = up;
            interfaces["down"] = down;
            interfaces["with_ip"] = withIp;
            interfaces["wan"] = wan;
            interfaces["edge"] = edge;
            interfaces["lan"] = lan;
        }

        // ── Tunnel interfaces ─────────────────────────────────────────────────────────────────
        // Appended to the same list with role=tunnel. They are interfaces on the box and belong in
        // the box's picture; PAN-OS just serves them from a different resource. When the vsys scope
        // hides them this document is empty, and the IPSec tunnels below still name them — so the
        // drawing degrades to "the tunnel exists, its interface was not readable" rather than to a
        // blank.
        for (const auto& e : nm::entriesOf(tunIfDoc))
        {
            if (!e.is_object())
                continue;
            const auto addrs = nm::interfaceAddresses(e);
            std::string joined;
            for (const auto& a : addrs)
                joined += (joined.empty() ? "" : ", ") + a;

            interfaces["list"].push_back({{"name", nm::str(e, "@name")},
                                          {"ip", joined},
                                          {"addresses", addrs},
                                          {"mode", "tunnel"},
                                          {"comment", nm::str(e, "comment")},
                                          {"role", "tunnel"},
                                          {"vpn_count", 0},
                                          {"gp_count", 0},
                                          {"admin_state", nm::adminState(e)}});
            interfaces["total"] = interfaces["list"].size();
        }

        // ── IPSec tunnels, now joined to their gateway's peer ─────────────────────────────────
        std::unordered_map<std::string, const nm::IkeGateway*> byName;
        for (const auto& g : ikeGws)
            byName[g.name] = &g;

        // Which tunnel rides which gateway, so a link can name the tunnel that carries it.
        std::unordered_map<std::string, std::string> tunnelOfGateway;

        {
            int enabled = 0, disabled = 0;
            for (const auto& e : nm::entriesOf(tunDoc))
            {
                if (!e.is_object())
                    continue;

                std::string gateway;
                std::string crypto;
                if (e.contains("auto-key") && e["auto-key"].is_object())
                {
                    const json& ak = e["auto-key"];
                    crypto = nm::str(ak, "ipsec-crypto-profile");
                    if (ak.contains("ike-gateway"))
                    {
                        for (const auto& g : nm::entriesOf(ak["ike-gateway"]))
                        {
                            const std::string n = nm::str(g, "@name");
                            if (n.empty())
                                continue;
                            gateway += (gateway.empty() ? "" : ", ") + n;
                        }
                    }
                }

                const std::string dis = nm::str(e, "disabled");
                const bool off = (dis == "yes" || dis == "true");
                off ? ++disabled : ++enabled;

                std::string monitor;
                if (e.contains("tunnel-monitor") && e["tunnel-monitor"].is_object())
                    monitor = nm::str(e["tunnel-monitor"], "enable");

                const std::string tunName = nm::str(e, "@name");
                if (!gateway.empty())
                    tunnelOfGateway[gateway] = tunName;

                // The peer, resolved through the gateway. This is the join the old model could not
                // make, and it is the whole reason a tunnel row can now say where it goes.
                json peer = nullptr;
                const auto g = byName.find(gateway);
                if (g != byName.end())
                    peer = {{"kind", nm::peerKindName(g->second->peer.kind)},
                            {"addr", g->second->peer.addr},
                            {"label", g->second->peer.label},
                            {"region", g->second->peer.region},
                            {"tenant", g->second->peer.tenant}};

                tunnels["list"].push_back({{"name", tunName},
                                           {"interface", nm::str(e, "tunnel-interface")},
                                           {"gateway", gateway},
                                           {"crypto", crypto},
                                           {"monitor", monitor},
                                           {"peer", peer},
                                           {"enabled", !off}});
            }
            tunnels["total"] = tunnels["list"].size();
            tunnels["enabled"] = enabled;
            tunnels["disabled"] = disabled;
        }

        // ── IKE gateways, and the links they imply ────────────────────────────────────────────
        for (const auto& g : ikeGws)
        {
            json row = {{"name", g.name},
                        {"interface", g.interfaceName},
                        {"local_ip", g.localIp},
                        {"version", g.version},
                        {"peer", {{"kind", nm::peerKindName(g.peer.kind)},
                                  {"addr", g.peer.addr},
                                  {"label", g.peer.label},
                                  {"region", g.peer.region},
                                  {"tenant", g.peer.tenant}}}};
            ike["list"].push_back(std::move(row));

            if (g.peer.kind == nm::PeerKind::Unknown || g.peer.addr.empty())
                continue;

            const auto tun = tunnelOfGateway.find(g.name);
            out["links"].push_back({{"device", oid},
                                    {"device_name", fw["name"]},
                                    {"site", devSite},
                                    {"interface", g.interfaceName},
                                    {"local_ip", g.localIp},
                                    {"gateway", g.name},
                                    {"tunnel", tun == tunnelOfGateway.end() ? std::string() : tun->second},
                                    {"kind", nm::peerKindName(g.peer.kind)},
                                    {"peer", g.peer.addr},
                                    {"label", g.peer.label},
                                    {"region", g.peer.region},
                                    {"tenant", g.peer.tenant}});
        }
        ike["total"] = ike["list"].size();

        // ── GlobalProtect ─────────────────────────────────────────────────────────────────────
        for (const auto& p : portals)
            gp["portals"].push_back({{"name", p.name},
                                     {"interface", p.interfaceName},
                                     {"local_ip", p.localIp},
                                     {"gateways", p.gateways}});
        for (const auto& g : gateways)
            gp["gateways"].push_back({{"name", g.name},
                                      {"interface", g.interfaceName},
                                      {"local_ip", g.localIp},
                                      {"tunnel_mode", g.tunnelMode},
                                      {"pools", g.pools}});

        fw["interfaces"] = std::move(interfaces);
        fw["tunnels"] = std::move(tunnels);
        fw["ike"] = std::move(ike);
        fw["gp"] = std::move(gp);
        out["devices"].push_back(std::move(fw));
    }

    // ── Shape ─────────────────────────────────────────────────────────────────────────────────
    //
    // Which end is the hub, decided from the links rather than declared by an operator. Two real
    // deployments have to be told apart and they are mirror images:
    //
    //   sase_hub   every firewall has its own Service Connection to the same tenant. The fabric is
    //              the centre and the firewalls are spokes around it — the common Prisma design.
    //   ngfw_hub   one firewall holds the connections and the others reach the fabric through it.
    //              That firewall is the centre; it is a classic hub-and-spoke with an on-prem core.
    //
    // The test is where the fan-out is. Count firewalls per tenant, and connections per firewall: a
    // tenant reached by several firewalls is a hub tenant; a firewall holding several tenants'
    // connections while its siblings hold none is a hub firewall. Neither, and it is flat.
    {
        std::unordered_map<std::string, std::vector<std::string>> devicesPerTenant;
        std::unordered_map<std::string, int> fabricLinksPerDevice;

        for (const auto& l : out["links"])
        {
            const std::string kind = l.value("kind", std::string());
            if (kind != "service_connection" && kind != "remote_network")
                continue;

            const std::string tenant = l.value("tenant", std::string());
            const std::string dev = l.value("device", std::string());
            ++fabricLinksPerDevice[dev];

            auto& v = devicesPerTenant[tenant];
            if (std::find(v.begin(), v.end(), dev) == v.end())
                v.push_back(dev);
        }

        std::size_t widestTenant = 0;
        for (const auto& [tenant, devs] : devicesPerTenant)
        {
            (void)tenant;
            widestTenant = std::max(widestTenant, devs.size());
        }

        int busiestDevice = 0;
        int devicesWithFabric = 0;
        for (const auto& [dev, n] : fabricLinksPerDevice)
        {
            (void)dev;
            busiestDevice = std::max(busiestDevice, n);
            ++devicesWithFabric;
        }

        std::string shape = "flat";
        if (widestTenant >= 2)
            shape = "sase_hub";
        else if (devicesWithFabric == 1 && busiestDevice >= 2 && out["devices"].size() > 1)
            shape = "ngfw_hub";
        else if (devicesWithFabric >= 1)
            shape = "edge";

        json tenants = json::object();
        for (const auto& [tenant, devs] : devicesPerTenant)
            tenants[tenant.empty() ? "unknown" : tenant] = devs;

        out["shape"] = {{"kind", shape},
                        {"tenants", std::move(tenants)},
                        {"fabric_devices", devicesWithFabric},
                        {"max_links_per_device", busiestDevice}};
    }

    sources["ngfw_devices"] = out["devices"].size();
    sources["interface_streams"] = ifStreams;
    sources["tunnel_streams"] = tunStreams;
    sources["ike_streams"] = ikeStreams;
    sources["gp_streams"] = gpStreams;
    sources["ngfw_links"] = out["links"].size();
    return out;
}

// ── The join ──────────────────────────────────────────────────────────────────────────────────
//
// Both estates name the same address, from opposite sides, and until now neither knew it.
//
//   Prisma side   service-connection "sherpain" → ipsec_tunnel "test" → ike gateway → peer 1.214.192.59
//   Customer side Sherpain-FW ethernet1/2 → 1.214.192.59/28
//
// The peer address on a Service Connection's IKE gateway is where Prisma Access dials, and that is
// the customer's own firewall. So an exact match between it and an address configured on a firewall
// in this scope is not a heuristic and not a resemblance: it is the two ends of one tunnel, each
// stated by the side that owns it. That is the strongest evidence this page has ever had for
// drawing a line between the fabric and a box in the rack.
//
// WHAT IT MEANS, precisely, because the difference matters on the one card an operator opens when
// the data centre has gone quiet:
//
//   It means the tunnel is BUILT — configured end to end, both sides agreeing on who the other is,
//   so traffic CAN flow. It does not mean traffic IS flowing. Neither the deployment API nor the
//   config APIs report tunnel state, IKE phase, or a byte counter; nothing collected here does. A
//   green "up" light would be an invention, and on this card it would be the expensive kind.
//
// A connection whose peer is an FQDN or `dynamic` cannot be matched at all, and is left unlinked
// rather than guessed at — an unresolved name is not evidence of anything.
void TopologyService::linkServiceConnections(nlohmann::json& out, nlohmann::json& sources)
{
    if (!out.contains("sase") || !out.contains("ngfw"))
        return;

    // Every address configured on every firewall in scope, without its prefix length: the config
    // says "1.214.192.59/28" and the far side names the host, not the subnet.
    struct Owner { std::string oid, name, iface; };
    std::unordered_map<std::string, Owner> owners;
    for (const auto& d : out["ngfw"]["devices"])
    {
        if (!d.is_object() || !d.contains("interfaces") || !d["interfaces"].is_object())
            continue;
        for (const auto& i : d["interfaces"].value("list", json::array()))
        {
            if (!i.is_object())
                continue;
            for (const auto& a : i.value("addresses", json::array()))
            {
                if (!a.is_string())
                    continue;
                std::string ip = a.get<std::string>();
                const auto slash = ip.find('/');
                if (slash != std::string::npos)
                    ip.resize(slash);
                if (ip.empty())
                    continue;
                // First interface to claim an address keeps it. A duplicate across two firewalls is
                // a misconfiguration in the estate, not something to resolve by picking the later one.
                owners.emplace(ip, Owner{d.value("oid", std::string()),
                                         d.value("name", std::string()),
                                         i.value("name", std::string())});
            }
        }
    }

    int linked = 0, total = 0;
    for (auto& t : out["sase"]["tenants"])
    {
        if (!t.is_object() || !t.contains("sc") || !t["sc"].is_object())
            continue;
        int tenantLinked = 0;
        for (auto& c : t["sc"]["connections"])
        {
            if (!c.is_object())
                continue;
            ++total;
            const std::string peer = c.value("peer_ip", std::string());
            if (peer.empty())
                continue;
            const auto it = owners.find(peer);
            if (it == owners.end())
                continue;

            c["linked"] = true;
            c["linked_device"] = it->second.oid;
            c["linked_device_name"] = it->second.name;
            c["linked_interface"] = it->second.iface;
            ++tenantLinked;
            ++linked;
        }
        t["sc"]["linked"] = tenantLinked;
    }

    sources["sc_linked"] = linked;
    sources["sc_connections"] = total;
    LOG_TRACE("service connections linked to firewall interfaces ({}/{})", linked, total);
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
