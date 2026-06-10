#pragma once

#include <cstdint>
#include <string>

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

    // When a v2c GET times out, retry the same host with the v3 credentials
    // below. Lets a single sweep cover both v2c and v3-only devices.
    bool         v3Fallback{true};
    SnmpV3Config v3;
};

} // namespace pz::snmpd
