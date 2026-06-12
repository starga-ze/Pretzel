#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pz::snmpd
{

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

    // SNMPv3 fallback is PER-IP ONLY: a host listed here is retried with its v3
    // credentials when the v2c probe times out. A host NOT listed simply ends at v2c
    // (no global default fallback). Keyed by device IP, set in the GUI
    // (settings -> SNMP) and stored as snmpd.service.scan.v3_devices[].
    std::map<std::string, SnmpV3Config> v3PerIp;

    // The v3 creds configured for an IP, or nullptr if the host has no override
    // (=> no v3 fallback for it).
    const SnmpV3Config* v3For(const std::string& ip) const
    {
        const auto it = v3PerIp.find(ip);
        return it != v3PerIp.end() ? &it->second : nullptr;
    }
};

} // namespace pz::snmpd
