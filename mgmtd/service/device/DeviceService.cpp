#include "service/device/DeviceService.h"

#include "db/Database.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <numeric>
#include <unordered_map>

namespace pz::mgmtd
{

namespace
{

// Per-IP record assembled from the two tables before grouping.
struct IpRecord
{
    std::string                  ip;
    bool                         hasSnmp{false};
    std::string                  sysName;
    std::string                  sysDescr;
    std::string                  sysObjectId;
    std::string                  sysContact;
    std::string                  sysLocation;
    std::uint32_t                sysUpTimeTicks{0};
    std::vector<std::string>     macs;
    std::vector<DeviceInterface> interfaces;

    std::string    snmpVendor;   // probe_devices.snmp_vendor (PEN-resolved by engined)
    std::string    hostMac;      // probe_devices.mac (ARP-learned)
    std::string    hostVendor;   // probe_devices.host_vendor (OUI-resolved by engined)
    nlohmann::json ifTable{nlohmann::json::array()};
    nlohmann::json lldp{nlohmann::json::array()};
};

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contains(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

// Whether a MAC is a usable hardware fingerprint for grouping. Excludes bogus and
// VIRTUAL MACs that are shared across unrelated devices and would cause false merges:
//   - empty / all-zero (devices report this for logical interfaces with no MAC)
//   - broadcast
//   - VRRP virtual (00:00:5e:00:01:xx) and HSRP virtual (00:00:0c:07:ac:xx /
//     00:00:0c:9f:fx) — shared by distinct routers in the same redundancy group.
// `mac` is expected lowercased.
bool isGroupingMac(const std::string& mac)
{
    if (mac.empty()) return false;
    if (mac == "00:00:00:00:00:00") return false;
    if (mac == "ff:ff:ff:ff:ff:ff") return false;
    if (mac.rfind("00:00:5e:00:01:", 0) == 0) return false; // VRRP
    if (mac.rfind("00:00:0c:07:ac:", 0) == 0) return false; // HSRP
    if (mac.rfind("00:00:0c:9f:f",   0) == 0) return false; // HSRPv2
    return true;
}

// IPv4 numeric ordering ("192.168.0.2" < "192.168.0.10"). Falls back to string
// compare for anything that does not parse as 4 octets.
std::uint32_t ipv4Key(const std::string& ip)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
        return (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

bool ipLess(const std::string& lhs, const std::string& rhs)
{
    const auto kl = ipv4Key(lhs), kr = ipv4Key(rhs);
    if (kl != kr) return kl < kr;
    return lhs < rhs;
}

// Three-way taxonomy:
//   host    — no SNMP (ICMP-reachable endpoint, e.g. laptop/PC)
//   network — SNMP-managed network gear (router/switch/firewall/ap/gateway)
//   server  — SNMP-managed endpoint (hypervisor/bmc/server/windows/linux/printer),
//             and the default for any SNMP responder we cannot positively identify
//             as network gear. These keyword sets are intentionally simple — tune as
//             the device population becomes clearer.
void classify(const IpRecord& rep, std::string& type, std::string& subtype)
{
    if (!rep.hasSnmp)
    {
        type = "host";
        subtype = "unknown";
        return;
    }

    const std::string s = toLower(rep.sysDescr + " " + rep.sysObjectId + " " + rep.sysName);

    // ── Network gear signatures (checked first) ──
    if (contains(s, "firewall") || contains(s, "fortigate") || contains(s, "palo alto") ||
        contains(s, "fortios") || contains(s, " asa") || contains(s, "srx") ||
        contains(s, "pan-os"))
        { type = "network"; subtype = "firewall"; return; }
    if (contains(s, "access point") || contains(s, "wireless") || contains(s, "aironet") ||
        contains(s, "unifi") || contains(s, "aruba ap"))
        { type = "network"; subtype = "ap"; return; }
    if (contains(s, "router") || contains(s, "routeros") || contains(s, "mikrotik") ||
        contains(s, " ios ") || contains(s, "cisco ios") || contains(s, "vyos"))
        { type = "network"; subtype = "router"; return; }
    if (contains(s, "switch") || contains(s, "catalyst") || contains(s, "procurve") ||
        contains(s, "nexus") || contains(s, "powerconnect") || contains(s, "powerswitch") ||
        contains(s, "force10") || contains(s, "networking os"))
        { type = "network"; subtype = "switch"; return; }
    if (contains(s, "gateway") || contains(s, "load balancer") || contains(s, "netscaler") ||
        contains(s, "f5 ") || contains(s, "big-ip"))
        { type = "network"; subtype = "gateway"; return; }

    // ── Server / endpoint signatures ──
    if (contains(s, "vmware") || contains(s, "esxi") || contains(s, "hypervisor") ||
        contains(s, "proxmox") || contains(s, "hyper-v"))
        { type = "server"; subtype = "hypervisor"; return; }
    if (contains(s, "idrac") || contains(s, "ilo") || contains(s, "ipmi") ||
        contains(s, "bmc") || contains(s, "supermicro") || contains(s, "ibmc") ||
        contains(s, "integrated management"))
        { type = "server"; subtype = "bmc"; return; }
    if (contains(s, "windows")) { type = "server"; subtype = "windows"; return; }
    if (contains(s, "linux") || contains(s, "ubuntu") || contains(s, "debian") ||
        contains(s, "centos") || contains(s, "red hat") || contains(s, "darwin") ||
        contains(s, "macos"))
        { type = "server"; subtype = "linux"; return; }
    if (contains(s, "printer") || contains(s, "laserjet") || contains(s, "jetdirect"))
        { type = "server"; subtype = "printer"; return; }
    if (contains(s, "server")) { type = "server"; subtype = "server"; return; }

    // SNMP-enabled but unrecognized: managed endpoint.
    type = "server";
    subtype = "unknown";
}

// ── Union-Find over IP indices ───────────────────────────────────────────────
struct DSU
{
    std::vector<int> parent;
    explicit DSU(int n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
    int find(int x) { return parent[x] == x ? x : (parent[x] = find(parent[x])); }
    void unite(int a, int b) { parent[find(a)] = find(b); }
};

} // namespace

std::vector<DeviceGroup> DeviceService::groups() const
{
    auto& db = pz::db::Database::instance();

    std::map<std::string, IpRecord> byIp;

    // Unified per-IP inventory: ICMP reachability + host MAC/vendor (ICMP stage) and
    // SNMP/API identity + interfaces + ifTable/LLDP + vendor (SNMP/API stage), all on
    // one row. hasSnmp is derived from whether any SNMP/API-stage data is present.
    for (const auto& row : db.queryRows(
             "SELECT ip, status, mac, host_vendor, sys_name, sys_descr, sys_object_id, "
             "sys_contact, sys_location, sys_uptime_ticks, interface_macs, interfaces, "
             "snmp_vendor, if_table, lldp_neighbors FROM probe_devices"))
    {
        if (row.size() < 15 || row[0].empty())
            continue;

        auto& rec          = byIp[row[0]];
        rec.ip             = row[0];
        // ICMP stage.
        rec.hostMac        = row[2];
        rec.hostVendor     = row[3];
        // SNMP / vendor-API stage.
        rec.sysName        = row[4];
        rec.sysDescr       = row[5];
        rec.sysObjectId    = row[6];
        rec.sysContact     = row[7];
        rec.sysLocation    = row[8];
        rec.sysUpTimeTicks = static_cast<std::uint32_t>(std::strtoul(row[9].c_str(), nullptr, 10));
        rec.snmpVendor     = row[12];

        auto ifT = nlohmann::json::parse(row[13], nullptr, false);
        if (ifT.is_array()) rec.ifTable = std::move(ifT);
        auto nb = nlohmann::json::parse(row[14], nullptr, false);
        if (nb.is_array()) rec.lldp = std::move(nb);

        // interface_macs is a JSONB array of MAC strings. Keep only real, groupable
        // MACs (drop all-zero/virtual) and dedupe — agents often repeat the same MAC
        // across every ifTable row.
        auto macsJson = nlohmann::json::parse(row[10], nullptr, false);
        if (macsJson.is_array())
        {
            for (const auto& m : macsJson)
            {
                if (!m.is_string())
                    continue;
                const std::string mac = toLower(m.get<std::string>());
                if (isGroupingMac(mac) &&
                    std::find(rec.macs.begin(), rec.macs.end(), mac) == rec.macs.end())
                    rec.macs.push_back(mac);
            }
        }

        // interfaces is a JSONB array of {ip, netmask, if_index, if_name}.
        auto ifJson = nlohmann::json::parse(row[11], nullptr, false);
        if (ifJson.is_array())
        {
            for (const auto& it : ifJson)
            {
                if (!it.is_object())
                    continue;
                DeviceInterface di;
                di.ip      = it.value("ip", "");
                di.netmask = it.value("netmask", "");
                di.ifIndex = it.value("if_index", 0u);
                di.ifName  = it.value("if_name", "");
                if (!di.ip.empty())
                    rec.interfaces.push_back(std::move(di));
            }
        }

        // A device is "SNMP/API-managed" if any identity/topology data was collected
        // (the SNMP/API stage ran and returned something). Drives host vs network/server
        // classification, grouping primary selection, and vendor source.
        rec.hasSnmp = !rec.sysName.empty() || !rec.sysObjectId.empty() ||
                      !rec.macs.empty() || !rec.interfaces.empty() || !rec.ifTable.empty();
    }

    if (byIp.empty())
        return {};

    // Index IPs and union those that share any interface MAC.
    std::vector<IpRecord*> recs;
    recs.reserve(byIp.size());
    std::unordered_map<std::string, int> idxByIp;
    for (auto& [ip, rec] : byIp)
    {
        idxByIp[ip] = static_cast<int>(recs.size());
        recs.push_back(&rec);
    }

    DSU dsu(static_cast<int>(recs.size()));
    std::unordered_map<std::string, int> firstIpForMac;
    for (int i = 0; i < static_cast<int>(recs.size()); ++i)
    {
        for (const auto& mac : recs[i]->macs)
        {
            auto it = firstIpForMac.find(mac);
            if (it == firstIpForMac.end())
                firstIpForMac[mac] = i;
            else
                dsu.unite(i, it->second);
        }
    }

    // Collect members per root.
    std::unordered_map<int, std::vector<int>> members;
    for (int i = 0; i < static_cast<int>(recs.size()); ++i)
        members[dsu.find(i)].push_back(i);

    std::vector<DeviceGroup> out;
    out.reserve(members.size());

    for (auto& [root, idxs] : members)
    {
        DeviceGroup g;

        // Gather IPs, union MACs, union interfaces (dedup by interface IP).
        std::vector<std::string> macSet;
        std::unordered_map<std::string, DeviceInterface> ifByIp;
        for (int idx : idxs)
        {
            g.ips.push_back(recs[idx]->ip);
            macSet.insert(macSet.end(), recs[idx]->macs.begin(), recs[idx]->macs.end());
            for (const auto& di : recs[idx]->interfaces)
                ifByIp.emplace(di.ip, di);
        }
        std::sort(g.ips.begin(), g.ips.end(), ipLess);
        std::sort(macSet.begin(), macSet.end());
        macSet.erase(std::unique(macSet.begin(), macSet.end()), macSet.end());
        g.interfaceMacs = std::move(macSet);

        for (auto& [ip, di] : ifByIp)
            g.interfaces.push_back(std::move(di));
        std::sort(g.interfaces.begin(), g.interfaces.end(),
                  [](const DeviceInterface& a, const DeviceInterface& b) {
                      return ipLess(a.ip, b.ip);
                  });

        // Primary: prefer an SNMP-bearing IP, then lowest IP.
        int primaryIdx = idxs.front();
        for (int idx : idxs)
        {
            const bool curSnmp  = recs[primaryIdx]->hasSnmp;
            const bool candSnmp = recs[idx]->hasSnmp;
            if ((candSnmp && !curSnmp) ||
                (candSnmp == curSnmp && ipLess(recs[idx]->ip, recs[primaryIdx]->ip)))
                primaryIdx = idx;
        }

        IpRecord& p = *recs[primaryIdx];
        g.primaryIp      = p.ip;
        g.hasSnmp        = p.hasSnmp;
        g.hostname       = p.sysName;
        g.sysDescr       = p.sysDescr;
        g.sysObjectId    = p.sysObjectId;
        g.sysContact     = p.sysContact;
        g.sysLocation    = p.sysLocation;
        g.sysUpTimeTicks = p.sysUpTimeTicks;
        g.ifTable        = std::move(p.ifTable);
        g.lldpNeighbors  = std::move(p.lldp);
        g.hostMac        = p.hostMac;
        g.vendor         = p.hasSnmp ? p.snmpVendor : p.hostVendor;

        classify(p, g.type, g.subtype);

        // Host subtype refinement from OUI vendor (engined-resolved): known AP vendors
        // → access point; otherwise leave the coarse type and surface the vendor.
        if (!p.hasSnmp && !g.vendor.empty())
        {
            const std::string v = toLower(g.vendor);
            if (contains(v, "ruckus") || contains(v, "aruba") || contains(v, "mist") ||
                contains(v, "meraki") || contains(v, "ubiquiti") || contains(v, "aerohive"))
                g.subtype = "ap";
        }

        out.push_back(std::move(g));
    }

    // Order: network → server → host; within a class, by primary IP.
    auto rank = [](const std::string& t) {
        if (t == "network") return 0;
        if (t == "server")  return 1;
        return 2;  // host
    };
    std::sort(out.begin(), out.end(), [&](const DeviceGroup& a, const DeviceGroup& b) {
        if (a.type != b.type)
            return rank(a.type) < rank(b.type);
        return ipLess(a.primaryIp, b.primaryIp);
    });

    LOG_DEBUG("DeviceService: {} group(s) from {} IP(s)", out.size(), recs.size());
    return out;
}

} // namespace pz::mgmtd
