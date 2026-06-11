#pragma once

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

class SnmpService
{
public:
    // Called by MgmtdRxRouter when SnmpResult IPC arrives.
    void handleSnmpResult(const std::string& payloadJson);

    // Returns a copy of the device map.
    std::map<std::string, SnmpDeviceInfo> devices() const;

    // Loads the persisted device inventory from the DB into memory. Called once at
    // startup so SNMP-discovered attributes survive a daemon restart (the live
    // alive/dead status still comes from ICMP at request time).
    void loadPersisted();

private:
    // Write-through replace of the devices table with the current map.
    void persist();

    std::map<std::string, SnmpDeviceInfo>  m_devices;
};

} // namespace pz::mgmtd
