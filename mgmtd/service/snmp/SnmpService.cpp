#include "service/snmp/SnmpService.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

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

    std::lock_guard<std::mutex> lock(m_mutex);
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

    LOG_INFO("SnmpService: updated device map size={}", m_devices.size());
}

std::map<std::string, SnmpDeviceInfo> SnmpService::devices() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_devices;
}

} // namespace pz::mgmtd
