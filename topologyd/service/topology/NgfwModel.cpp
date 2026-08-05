#include "service/topology/NgfwModel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace pz::topologyd::ngfw
{

using json = nlohmann::json;

namespace
{

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == sep)
        {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    out.push_back(cur);
    return out;
}

// The address without its prefix length or port — "1.214.192.59/28" and "1.214.192.59:12929" both
// reduce to the host, which is the only part any comparison here cares about.
std::string bareHost(const std::string& s)
{
    std::string out = s;
    const auto slash = out.find('/');
    if (slash != std::string::npos)
        out = out.substr(0, slash);
    const auto colon = out.find(':');
    // Only strip a port from something that is otherwise dotted-quad; an IPv6 literal is all colons.
    if (colon != std::string::npos && out.find('.') != std::string::npos && out.find(':') == out.rfind(':'))
        out = out.substr(0, colon);
    return out;
}

bool parseV4(const std::string& s, unsigned octets[4])
{
    const auto parts = split(bareHost(s), '.');
    if (parts.size() != 4)
        return false;
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (parts[i].empty() || parts[i].size() > 3)
            return false;
        for (char c : parts[i])
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
        const long v = std::strtol(parts[i].c_str(), nullptr, 10);
        if (v < 0 || v > 255)
            return false;
        octets[i] = static_cast<unsigned>(v);
    }
    return true;
}

}

const char* ifRoleName(IfRole r)
{
    switch (r)
    {
    case IfRole::Wan:
        return "wan";
    case IfRole::Edge:
        return "edge";
    case IfRole::Lan:
        return "lan";
    case IfRole::Loopback:
        return "loopback";
    case IfRole::Tunnel:
        return "tunnel";
    case IfRole::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* peerKindName(PeerKind k)
{
    switch (k)
    {
    case PeerKind::ServiceConnection:
        return "service_connection";
    case PeerKind::RemoteNetwork:
        return "remote_network";
    case PeerKind::External:
        return "external";
    case PeerKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

bool isPrivateV4(const std::string& cidrOrIp)
{
    unsigned o[4] = {0, 0, 0, 0};
    if (!parseV4(cidrOrIp, o))
        return false;

    if (o[0] == 10)
        return true;                                   // 10/8
    if (o[0] == 172 && o[1] >= 16 && o[1] <= 31)
        return true;                                   // 172.16/12
    if (o[0] == 192 && o[1] == 168)
        return true;                                   // 192.168/16
    if (o[0] == 100 && o[1] >= 64 && o[1] <= 127)
        return true;                                   // 100.64/10 CGNAT
    if (o[0] == 169 && o[1] == 254)
        return true;                                   // link-local
    if (o[0] == 127)
        return true;                                   // loopback
    return false;
}

// Prisma Access publishes every tunnel endpoint under gpcloudservice.com, and the labels in between
// are structured rather than decorative:
//
//   sherpain-hq . south-korea . sc . osy5synss5 . gpcloudservice . com
//   south-korea-aspen . rn . nnjy2ycyn . gpcloudservice . com
//
// Reading right to left from the `sc`/`rn` marker is what makes both shapes work with one rule: the
// label after the marker is always the tenant, everything before it is location, however many labels
// the location happens to use.
Peer classifyPeer(const std::string& addr)
{
    Peer p;
    p.addr = addr;
    if (addr.empty())
        return p;

    const std::string host = lower(bareHost(addr));

    unsigned o[4] = {0, 0, 0, 0};
    if (parseV4(host, o))
    {
        p.kind = PeerKind::External;
        p.label = bareHost(addr);
        return p;
    }

    if (host.find("gpcloudservice.com") == std::string::npos)
    {
        // A hostname we cannot place. It is still a real peer and belongs in the picture — it just
        // is not Prisma, and saying so is better than filing it under a fabric it has nothing to do
        // with. `sc-fqdn-eval` and other half-finished entries land here too, correctly.
        p.kind = PeerKind::External;
        p.label = host;
        return p;
    }

    const auto labels = split(host, '.');
    for (std::size_t i = 0; i + 1 < labels.size(); ++i)
    {
        const bool sc = (labels[i] == "sc");
        const bool rn = (labels[i] == "rn");
        if (!sc && !rn)
            continue;

        p.kind = sc ? PeerKind::ServiceConnection : PeerKind::RemoteNetwork;
        p.tenant = labels[i + 1];

        // Everything left of the marker is where it lives. The last of those labels is the region
        // for a Service Connection ("sherpain-hq.south-korea.sc.…"); a Remote Network folds region
        // and SPN into one ("south-korea-aspen.rn.…"), so the whole label is the readable name and
        // the region is only recoverable when a separate label carries it.
        if (i >= 2)
        {
            p.region = labels[i - 1];
            for (std::size_t j = 0; j + 1 < i; ++j)
                p.label += (p.label.empty() ? "" : ".") + labels[j];
        }
        else if (i == 1)
        {
            p.label = labels[0];
        }

        if (p.label.empty())
            p.label = host;
        return p;
    }

    // Under gpcloudservice.com but not an sc/rn endpoint — a portal or gateway hostname, say.
    p.kind = PeerKind::External;
    p.label = host;
    return p;
}

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

std::string str(const json& o, const char* key)
{
    if (!o.is_object() || !o.contains(key) || o[key].is_null())
        return {};
    const auto& v = o[key];
    return v.is_string() ? v.get<std::string>() : v.dump();
}

namespace
{

// The address out of a {"ip":{"ipv4":"…"}} or {"ip":"…"} node — PAN-OS uses both.
std::string ipOf(const json& node)
{
    if (!node.is_object())
        return node.is_string() ? node.get<std::string>() : std::string();
    if (node.contains("ipv4"))
        return str(node, "ipv4");
    if (node.contains("ip"))
        return ipOf(node["ip"]);
    return {};
}

// The {interface, ip} pair that PAN-OS hangs off local-address, portal-config and gateways alike.
void readLocalAddress(const json& node, std::string& iface, std::string& ip)
{
    if (!node.is_object())
        return;
    iface = str(node, "interface");
    if (node.contains("ip"))
        ip = ipOf(node["ip"]);
}

}

std::vector<std::string> interfaceAddresses(const json& entry)
{
    std::vector<std::string> out;
    for (const char* mode : {"layer3", "layer2", "tap", "ha", "virtual-wire"})
    {
        if (!entry.contains(mode) || !entry[mode].is_object())
            continue;
        const json& m = entry[mode];
        if (!m.contains("ip"))
            continue;

        for (const auto& a : entriesOf(m))
        {
            const std::string addr = str(a, "@name");
            if (!addr.empty())
                out.push_back(addr);
        }
        if (!out.empty())
            return out;

        if (m["ip"].is_object())
        {
            for (const auto& a : entriesOf(m["ip"]))
            {
                const std::string addr = str(a, "@name");
                if (!addr.empty())
                    out.push_back(addr);
            }
        }
        if (!out.empty())
            return out;
    }
    return out;
}

std::string interfaceMode(const json& entry)
{
    for (const char* mode : {"layer3", "layer2", "tap", "ha", "virtual-wire", "aggregate-group"})
        if (entry.contains(mode))
            return mode;
    return {};
}

std::string adminState(const json& entry)
{
    if (entry.contains("link-state"))
    {
        const std::string ls = lower(str(entry, "link-state"));
        if (ls.find("down") != std::string::npos)
            return "down";
        if (ls.find("up") != std::string::npos)
            return "up";
    }
    const std::string disabled = str(entry, "disabled");
    if (disabled == "yes" || disabled == "true")
        return "down";
    return "up";
}

std::vector<IkeGateway> readIkeGateways(const json& doc)
{
    std::vector<IkeGateway> out;
    for (const auto& e : entriesOf(doc))
    {
        if (!e.is_object())
            continue;

        IkeGateway g;
        g.name = str(e, "@name");
        if (e.contains("local-address"))
            readLocalAddress(e["local-address"], g.interfaceName, g.localIp);

        // The peer is an address or an FQDN, under its own key either way.
        if (e.contains("peer-address") && e["peer-address"].is_object())
        {
            const json& pa = e["peer-address"];
            std::string addr = str(pa, "ip");
            if (addr.empty())
                addr = str(pa, "fqdn");
            if (addr.empty())
                addr = ipOf(pa);
            g.peer = classifyPeer(addr);
        }

        if (e.contains("protocol") && e["protocol"].is_object())
            g.version = str(e["protocol"], "version");

        out.push_back(std::move(g));
    }
    return out;
}

std::vector<GpPortal> readGpPortals(const json& doc)
{
    std::vector<GpPortal> out;
    for (const auto& e : entriesOf(doc))
    {
        if (!e.is_object())
            continue;

        GpPortal p;
        p.name = str(e, "@name");

        if (e.contains("portal-config") && e["portal-config"].is_object())
            readLocalAddress(e["portal-config"]["local-address"], p.interfaceName, p.localIp);

        // The gateway list a client is handed. Nested three deep because a portal serves several
        // client configurations and each one names its own gateways; flattened to the union, since
        // the question the picture answers is "which gateways does this portal send users to".
        if (e.contains("client-config") && e["client-config"].is_object() &&
            e["client-config"].contains("configs"))
        {
            for (const auto& cfg : entriesOf(e["client-config"]["configs"]))
            {
                if (!cfg.is_object() || !cfg.contains("gateways"))
                    continue;
                const json& gws = cfg["gateways"];
                for (const char* kind : {"external", "internal"})
                {
                    if (!gws.contains(kind) || !gws[kind].is_object() || !gws[kind].contains("list"))
                        continue;
                    for (const auto& g : entriesOf(gws[kind]["list"]))
                    {
                        if (!g.is_object())
                            continue;
                        const std::string name = str(g, "@name");
                        std::string addr = g.contains("ip") ? ipOf(g["ip"]) : std::string();
                        if (addr.empty())
                            addr = str(g, "fqdn");

                        std::string label = name;
                        if (!addr.empty())
                            label += " @ " + addr;
                        if (label.empty())
                            continue;
                        // The same gateway is normally listed by every client configuration; the
                        // union is what is wanted, not one entry per mention.
                        if (std::find(p.gateways.begin(), p.gateways.end(), label) == p.gateways.end())
                            p.gateways.push_back(label);
                    }
                }
            }
        }

        out.push_back(std::move(p));
    }
    return out;
}

std::vector<GpGateway> readGpGateways(const json& doc)
{
    std::vector<GpGateway> out;
    for (const auto& e : entriesOf(doc))
    {
        if (!e.is_object())
            continue;

        GpGateway g;
        g.name = str(e, "@name");
        if (e.contains("local-address"))
            readLocalAddress(e["local-address"], g.interfaceName, g.localIp);

        const std::string tm = str(e, "tunnel-mode");
        g.tunnelMode = (tm == "yes" || tm == "true");

        // The client IP pools, which are the only part of a gateway that shows up as routing on the
        // LAN side — worth carrying because "where do VPN users appear from" is a real question.
        if (e.contains("remote-user-tunnel-configs"))
        {
            for (const auto& c : entriesOf(e["remote-user-tunnel-configs"]))
            {
                if (!c.is_object() || !c.contains("ip-pool"))
                    continue;
                const json& pool = c["ip-pool"];
                if (!pool.is_object() || !pool.contains("member"))
                    continue;
                const json& m = pool["member"];
                if (m.is_array())
                {
                    for (const auto& v : m)
                        if (v.is_string())
                            g.pools.push_back(v.get<std::string>());
                }
                else if (m.is_string())
                {
                    g.pools.push_back(m.get<std::string>());
                }
            }
        }

        out.push_back(std::move(g));
    }
    return out;
}

}
