#include "service/scan/ScanService.h"

#include "service/EnginedServiceManager.h"

#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pz::engined
{

void ScanService::handleEvent(EnginedServiceManager& /*serviceManager*/,
                              const ScanEvent& event)
{
    if (event.type() != ScanEventType::ReceiveSnmpResult)
    {
        return;
    }

    const pz::ipc::IpcMessage* in = event.message();
    if (!in || in->getPayload().empty())
    {
        LOG_WARN("ScanService: empty SnmpResult — dropping");
        return;
    }

    const auto& pl = in->getPayload();
    persistDevices(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
}

void ScanService::persistDevices(const std::string& payloadJson)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("ScanService: failed to parse SnmpResult payload: {}", e.what());
        return;
    }

    const auto& devices = root.value("devices", nlohmann::json::array());

    // The SnmpResult is an authoritative full snapshot of the scanned IPs, so replace
    // the snmp_devices table wholesale. exec() fails soft when the DB is down.
    auto& db = pz::db::Database::instance();
    db.exec("DELETE FROM snmp_devices");

    int written = 0;
    for (const auto& d : devices)
    {
        const std::string ip = d.value("ip", "");
        if (ip.empty())
            continue;

        // interface_macs is a JSON array (ifPhysAddress MAC set); store as jsonb.
        const std::string macs =
            d.value("interface_macs", nlohmann::json::array()).dump();

        const bool ok = db.exec(
            "INSERT INTO snmp_devices "
            "(ip, sys_name, sys_descr, sys_object_id, sys_contact, sys_location, "
            " sys_uptime_ticks, interface_macs) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8::jsonb) "
            "ON CONFLICT (ip) DO UPDATE SET "
            "  sys_name = EXCLUDED.sys_name, sys_descr = EXCLUDED.sys_descr, "
            "  sys_object_id = EXCLUDED.sys_object_id, sys_contact = EXCLUDED.sys_contact, "
            "  sys_location = EXCLUDED.sys_location, "
            "  sys_uptime_ticks = EXCLUDED.sys_uptime_ticks, "
            "  interface_macs = EXCLUDED.interface_macs, updated_at = now()",
            {ip,
             d.value("sys_name", ""),
             d.value("sys_descr", ""),
             d.value("sys_object_id", ""),
             d.value("sys_contact", ""),
             d.value("sys_location", ""),
             std::to_string(d.value("sys_up_time_ticks", 0u)),
             macs});
        if (ok)
            ++written;
    }

    LOG_INFO("ScanService: persisted {} SNMP device(s) to snmp_devices", written);
}

} // namespace pz::engined
