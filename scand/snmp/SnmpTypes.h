#pragma once

#include "lldp/LldpTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pz::scand
{

struct SnmpInterface
{
    std::string ip;
    std::string netmask;
    uint32_t ifIndex{0};
    std::string ifName;
};

struct SnmpIfEntry
{
    uint32_t ifIndex{0};
    std::string name;
    std::string descr;
    std::string alias;
    int type{0};
    uint64_t speed{0};
    int operStatus{0};
    std::string mac;
};

struct SnmpArpEntry
{
    std::string ip;
    std::string mac;
    uint32_t ifIndex{0};
};

struct SnmpDevice
{
    std::string ip;
    std::string sysDescr;
    std::string sysName;
    std::string sysContact;
    std::string sysLocation;
    std::string sysObjectId;
    uint32_t sysUpTimeTicks{0};

    std::vector<std::string> interfaceMacs;

    std::vector<SnmpInterface> interfaces;

    std::vector<SnmpIfEntry> ifTable;

    std::vector<LldpNeighbor> lldpNeighbors;

    std::vector<SnmpArpEntry> arpEntries;
};

enum class SnmpVersion
{
    V2c,
    V3,
};

enum class SnmpSecurityLevel
{
    NoAuthNoPriv,
    AuthNoPriv,
    AuthPriv,
};

struct SnmpV3Config
{
    std::string user;
    std::string authProtocol{"SHA"};
    std::string authPassword;
    std::string privProtocol{"AES"};
    std::string privPassword;
    SnmpSecurityLevel level{SnmpSecurityLevel::AuthPriv};
};

struct SnmpScanConfig
{
    std::string community{"public"};
    uint16_t port{161};
    int timeoutMs{1500};
    int retries{1};
    int maxConcurrent{10};

    int v2cProbeTimeoutMs{700};
    int v2cProbeRetries{0};

    std::map<std::string, std::string> v2cPerIp;

    const std::string& communityFor(const std::string& ip) const
    {
        const auto it = v2cPerIp.find(ip);
        return it != v2cPerIp.end() ? it->second : community;
    }

    bool isRegistered(const std::string& ip) const
    {
        return v2cPerIp.count(ip) != 0 || v3PerIp.count(ip) != 0;
    }

    std::map<std::string, SnmpV3Config> v3PerIp;

    const SnmpV3Config* v3For(const std::string& ip) const
    {
        const auto it = v3PerIp.find(ip);
        return it != v3PerIp.end() ? &it->second : nullptr;
    }
};

}
