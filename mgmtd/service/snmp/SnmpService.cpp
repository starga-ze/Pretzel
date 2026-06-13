#include "service/snmp/SnmpService.h"

#include "db/Database.h"

#include <cstdlib>

namespace pz::mgmtd
{

std::map<std::string, SnmpDeviceInfo> SnmpService::devices() const
{
    // engined writes snmp_devices; mgmtd reads it live here. (interface_macs is also
    // stored but not surfaced by /api/devices yet.)
    const auto rows = pz::db::Database::instance().queryRows(
        "SELECT ip, sys_name, sys_descr, sys_object_id, sys_contact, sys_location, "
        "sys_uptime_ticks FROM snmp_devices");

    std::map<std::string, SnmpDeviceInfo> out;
    for (const auto& row : rows)
    {
        if (row.size() < 7 || row[0].empty())
            continue;

        SnmpDeviceInfo info;
        info.sysName        = row[1];
        info.sysDescr       = row[2];
        info.sysObjectId    = row[3];
        info.sysContact     = row[4];
        info.sysLocation    = row[5];
        info.sysUpTimeTicks =
            static_cast<uint32_t>(std::strtoul(row[6].c_str(), nullptr, 10));

        out[row[0]] = std::move(info);
    }

    return out;
}

} // namespace pz::mgmtd
