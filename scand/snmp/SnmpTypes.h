#pragma once

#include "lldp/LldpTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pz::scand
{

// One IP-bearing interface from the ip-MIB ipAddrTable (ipAdEntAddr). Covers
// firewall WAN/LAN interfaces, switch SVIs (VLAN interfaces), router interfaces —
// they all surface here as an IP + netmask on an ifIndex.
struct SnmpInterface
{
    std::string ip;         // ipAdEntAddr
    std::string netmask;    // ipAdEntNetMask
    uint32_t    ifIndex{0}; // ipAdEntIfIndex
    std::string ifName;     // ifDescr for that ifIndex
};

// One row of the IF-MIB ifTable / ifXTable — a physical or logical interface.
struct SnmpIfEntry
{
    uint32_t    ifIndex{0};
    std::string name;       // ifName (ifXTable) or ifDescr
    std::string descr;      // ifDescr
    std::string alias;      // ifAlias (ifXTable) — admin-configured label
    int         type{0};    // ifType (IANAifType)
    uint64_t    speed{0};   // ifHighSpeed (Mbps)
    int         operStatus{0}; // ifOperStatus (1=up,2=down,...)
    std::string mac;        // ifPhysAddress
};

// One ARP / ip-MIB ipNetToMedia entry: an IP↔MAC the device has learned. Used to
// discover the MAC (hence vendor) of SNMP-less hosts.
struct SnmpArpEntry
{
    std::string ip;
    std::string mac;
    uint32_t    ifIndex{0};
};

struct SnmpDevice
{
    std::string ip;
    std::string sysDescr;
    std::string sysName;
    std::string sysContact;
    std::string sysLocation;
    std::string sysObjectId;
    uint32_t    sysUpTimeTicks{0};

    // ifPhysAddress (ifTable col 6) MACs — the device's interface MAC set. Used as a
    // hardware fingerprint to group the multiple IPs of one physical device, since many
    // devices (e.g. Palo Alto) don't expose the standard ip-MIB address tables.
    std::vector<std::string> interfaceMacs;

    // IP-bearing interfaces (ipAddrTable): firewall up/down interfaces, switch SVIs.
    std::vector<SnmpInterface> interfaces;

    // IF-MIB ifTable/ifXTable interface inventory (ports, NICs).
    std::vector<SnmpIfEntry> ifTable;

    // LLDP neighbors (topology edges).
    std::vector<LldpNeighbor> lldpNeighbors;

    // ARP / ipNetToMedia entries (IP↔MAC the device has learned).
    std::vector<SnmpArpEntry> arpEntries;
};

// SNMP protocol version used for a given request.
enum class SnmpVersion
{
    V2c,
    V3,
};

// SNMPv3 USM security level.
enum class SnmpSecurityLevel
{
    NoAuthNoPriv,
    AuthNoPriv,
    AuthPriv,
};

// SNMPv3 (USM) credentials. Used when a v2c probe times out and v3 fallback
// is enabled, or when a device only speaks v3.
struct SnmpV3Config
{
    std::string       user;
    std::string       authProtocol{"SHA"};   // "SHA" | "MD5"
    std::string       authPassword;
    std::string       privProtocol{"AES"};   // "AES" | "DES"
    std::string       privPassword;
    SnmpSecurityLevel level{SnmpSecurityLevel::AuthPriv};
};

struct SnmpScanConfig
{
    std::string  community{"public"};
    uint16_t     port{161};
    int          timeoutMs{1500};   // used by the v3 fetch
    int          retries{1};        // used by the v3 fetch
    int          maxConcurrent{10};

    // The v2c probe only needs to detect "does this host speak v2c?", so it
    // uses a short timeout / no retries. v3-only hosts then fall back quickly
    // instead of burning timeoutMs*(retries+1) before v3 even starts.
    int          v2cProbeTimeoutMs{700};
    int          v2cProbeRetries{0};

    // Per-IP v2c community overrides. A host listed here is probed with its own
    // community instead of the global `community` above. Set in the GUI
    // (settings -> SNMP, method "SNMPv2c") and stored as
    // scand.service.scan.v2c_devices[].
    std::map<std::string, std::string> v2cPerIp;

    // The community to use for `ip`. SNMP is opt-in per device (see isRegistered),
    // so this is only ever consulted for an already-registered IP: its own v2c
    // override if set, otherwise the global default. Returns a reference into a
    // member that outlives the snmp session setup, so the pointer handed to
    // net-snmp stays valid.
    const std::string& communityFor(const std::string& ip) const
    {
        const auto it = v2cPerIp.find(ip);
        return it != v2cPerIp.end() ? it->second : community;
    }

    // True if `ip` has an explicit v2c or v3 entry. SNMP is opt-in per device — an
    // IP with no entry here is never probed by SnmpEngine, regardless of how broad
    // the IP list handed to SnmpEngine::startScan is. A device on the "api" scan
    // method instead goes straight to ApiEngine and never appears here at all —
    // each device has exactly one scan method, chosen in the GUI.
    bool isRegistered(const std::string& ip) const
    {
        return v2cPerIp.count(ip) != 0 ||
               v3PerIp.count(ip)  != 0;
    }

    // SNMPv3 fallback is PER-IP ONLY: a host listed here is retried with its v3
    // credentials when the v2c probe times out. A host NOT listed simply ends at v2c
    // (no global default fallback). Keyed by device IP, set in the GUI
    // (settings -> SNMP) and stored as scand.service.scan.v3_devices[].
    std::map<std::string, SnmpV3Config> v3PerIp;

    // The v3 creds configured for an IP, or nullptr if the host has no override
    // (=> no v3 fallback for it).
    const SnmpV3Config* v3For(const std::string& ip) const
    {
        const auto it = v3PerIp.find(ip);
        return it != v3PerIp.end() ? &it->second : nullptr;
    }
};

} // namespace pz::scand
