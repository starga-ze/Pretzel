#pragma once

#include <map>
#include <mutex>
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

    // Returns a snapshot copy of the device map (thread-safe).
    std::map<std::string, SnmpDeviceInfo> devices() const;

private:
    mutable std::mutex                     m_mutex;
    std::map<std::string, SnmpDeviceInfo>  m_devices;
};

} // namespace pz::mgmtd
