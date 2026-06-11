#include "service/snmp/SnmpService.h"

#include "db/Database.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <cstdlib>

namespace pz::mgmtd
{

void SnmpService::handleSnmpResult(const std::string& payloadJson)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("SnmpService: failed to parse SnmpResult payload: {}", e.what());
        return;
    }

    m_devices.clear();

    for (const auto& d : root.value("devices", nlohmann::json::array()))
    {
        const std::string ip = d.value("ip", "");
        if (ip.empty()) continue;

        SnmpDeviceInfo info;
        info.sysName       = d.value("sys_name",          "");
        info.sysDescr      = d.value("sys_descr",         "");
        info.sysObjectId   = d.value("sys_object_id",     "");
        info.sysContact    = d.value("sys_contact",       "");
        info.sysLocation   = d.value("sys_location",      "");
        info.sysUpTimeTicks = d.value("sys_up_time_ticks", 0u);

        m_devices[ip] = std::move(info);
    }

    persist();

    LOG_INFO("SnmpService: updated device map size={}", m_devices.size());
}

void SnmpService::persist()
{
    // The SnmpResult is an authoritative full snapshot, so replace the table
    // wholesale. exec() fails soft when the DB is down — the in-memory map remains
    // the source of truth for /api/devices either way.
    auto& db = pz::db::Database::instance();
    db.exec("DELETE FROM devices");

    for (const auto& [ip, info] : m_devices)
    {
        db.exec(
            "INSERT INTO devices "
            "(ip, sys_name, sys_descr, sys_object_id, sys_contact, sys_location, sys_uptime_ticks) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7)",
            {ip, info.sysName, info.sysDescr, info.sysObjectId,
             info.sysContact, info.sysLocation, std::to_string(info.sysUpTimeTicks)});
    }
}

void SnmpService::loadPersisted()
{
    const auto rows = pz::db::Database::instance().queryRows(
        "SELECT ip, sys_name, sys_descr, sys_object_id, sys_contact, sys_location, "
        "sys_uptime_ticks FROM devices");

    m_devices.clear();

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

        m_devices[row[0]] = std::move(info);
    }

    if (!m_devices.empty())
        LOG_INFO("SnmpService: loaded persisted device inventory size={}", m_devices.size());
}

std::map<std::string, SnmpDeviceInfo> SnmpService::devices() const
{
    return m_devices;
}

} // namespace pz::mgmtd
