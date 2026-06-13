#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace pz::mgmtd
{

struct SnmpDeviceInfo
{
    std::string sysName;
    std::string sysDescr;
    std::string sysObjectId;
    std::string sysContact;
    std::string sysLocation;
    uint32_t    sysUpTimeTicks{0};
};

// Read-only view of the SNMP inventory. engined (the single DB writer) persists scan
// results into the snmp_devices table; mgmtd only reads it here for /api/devices.
class SnmpService
{
public:
    // Returns the current SNMP inventory, keyed by IP, read live from snmp_devices.
    std::map<std::string, SnmpDeviceInfo> devices() const;
};

} // namespace pz::mgmtd
