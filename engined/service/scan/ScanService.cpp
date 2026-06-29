#include "service/scan/ScanService.h"

#include "service/EnginedServiceManager.h"
#include "router/EnginedTxRouter.h"

#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "config/Config.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pz::engined
{

namespace
{

// Overridable via "service"."scan" in the running-config (section "engined").
std::chrono::milliseconds pollInterval()
{
    const auto& s = pz::config::Config::serviceSection("engined", "scan");
    return std::chrono::seconds(s.value("poll_interval_sec", 60));
}

std::chrono::milliseconds responseTimeout()
{
    const auto& s = pz::config::Config::serviceSection("engined", "scan");
    return std::chrono::seconds(s.value("response_timeout_sec", 30));
}

// IPs explicitly registered in the scan settings (scand.service.scan device
// lists). SNMP is opt-in: only these IPs are ever sent to scand for scanning,
// regardless of ICMP discovery, so an operator-added device is polled even when
// it does not answer ping, and an unregistered ICMP-alive host is never probed.
// Disabled rows (enabled:false) are skipped; default true for backward
// compatibility. Deduplicated as a defensive measure — each device is meant to
// have exactly one scan method (v2c_devices / v3_devices / api_devices, chosen in
// the GUI), but this still guards against a stale config listing the same IP
// under more than one.
std::vector<std::string> registeredDeviceIps()
{
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    const auto& scan = pz::config::Config::serviceSection("scand", "scan");
    for (const char* key : {"v2c_devices", "v3_devices", "api_devices"})
    {
        if (!scan.contains(key) || !scan[key].is_array())
            continue;
        for (const auto& d : scan[key])
        {
            if (!d.is_object() || !d.value("enabled", true))
                continue;
            const std::string ip = d.value("ip", std::string());
            if (!ip.empty() && seen.insert(ip).second)
                out.push_back(ip);
        }
    }
    return out;
}

} // namespace

void ScanService::start()
{
    m_pending    = false;
    m_lastPollAt = {};
    LOG_INFO("ScanService (engined) start");
}

std::unique_ptr<EnginedEvent>
ScanService::schedule(std::chrono::steady_clock::time_point now)
{
    if (m_pending)
    {
        if (now - m_requestedAt >= responseTimeout())
        {
            LOG_WARN("ScanResult timed out — clearing pending");
            m_pending = false;
        }
        return nullptr;
    }

    if (m_lastPollAt.time_since_epoch().count() == 0 ||
        now - m_lastPollAt >= pollInterval())
    {
        m_lastPollAt = now;
        return std::make_unique<ScanEvent>(ScanEventType::TriggerScan);
    }

    return nullptr;
}

void ScanService::handleEvent(EnginedServiceManager& serviceManager,
                              const ScanEvent& event)
{
    switch (event.type())
    {
    case ScanEventType::TriggerScan:
        sendScanRequest(serviceManager);
        break;

    case ScanEventType::ReceiveScanResult:
    {
        m_pending = false;

        const pz::ipc::IpcMessage* in = event.message();
        if (!in || in->getPayload().empty())
        {
            LOG_WARN("empty ScanResult — dropping");
            return;
        }

        const auto& pl = in->getPayload();
        persistDevices(serviceManager,
                       std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
        break;
    }

    default:
        LOG_WARN("unhandled event (type={})",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void ScanService::sendScanRequest(EnginedServiceManager& serviceManager)
{
    // SNMP scan set = explicitly-registered devices only (scand.service.scan
    // v2c/v3/api device lists). ICMP-discovered "alive" hosts with no registration
    // are intentionally excluded — SNMP is opt-in per device, never a blind sweep
    // of every alive IP. ICMP reachability itself is tracked independently by
    // ProbeService and is unaffected by this list.
    std::vector<std::string> ips = registeredDeviceIps();

    if (ips.empty())
    {
        LOG_DEBUG("no registered SNMP/API devices — skipping scan trigger");
        return;
    }

    nlohmann::json req;
    req["ips"] = ips;
    const std::string json = req.dump();

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Engined,
        pz::ipc::IpcDaemon::Scand,
        pz::ipc::IpcCmd::ScanRequest,
        static_cast<std::uint32_t>(json.size()),
        pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(json.data()), json.size());

    m_pending     = true;
    m_requestedAt = std::chrono::steady_clock::now();

    LOG_INFO("sending ScanRequest to scand (ips={})", ips.size());

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void ScanService::persistDevices(EnginedServiceManager& serviceManager,
                                 const std::string& payloadJson)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ScanResult payload (error={})", e.what());
        return;
    }

    const auto& devices = root.value("devices", nlohmann::json::array());

    // The ScanResult is an authoritative full snapshot of the scanned set. ScanService
    // owns only the SNMP/API columns of probe_devices (ProbeService owns reachability +
    // row lifecycle), so clear those columns across the table, then re-apply responders.
    // This mirrors the old wholesale snmp_devices replace without touching ICMP data.
    auto& db = pz::db::Database::instance();
    db.exec("UPDATE probe_devices SET "
            "  sys_name = NULL, sys_descr = NULL, sys_object_id = NULL, "
            "  sys_contact = NULL, sys_location = NULL, sys_uptime_ticks = NULL, "
            "  interface_macs = NULL, interfaces = NULL, if_table = NULL, "
            "  lldp_neighbors = NULL, arp_entries = NULL, snmp_vendor = NULL");

    int written = 0;
    for (const auto& d : devices)
    {
        const std::string ip = d.value("ip", "");
        if (ip.empty())
            continue;

        // JSON arrays stored as jsonb: interface_macs (ifPhysAddress fingerprint),
        // interfaces (ipAddrTable), if_table (IF-MIB), lldp_neighbors, arp_entries.
        const std::string macs =
            d.value("interface_macs", nlohmann::json::array()).dump();
        const std::string ifaces =
            d.value("interfaces", nlohmann::json::array()).dump();
        const std::string ifTable =
            d.value("if_table", nlohmann::json::array()).dump();
        const std::string neighbors =
            d.value("lldp_neighbors", nlohmann::json::array()).dump();
        const std::string arp =
            d.value("arp_entries", nlohmann::json::array()).dump();

        // Vendor from sysObjectID enterprise number (engined owns vendor resolution).
        const std::string sysObjectId = d.value("sys_object_id", "");
        const std::string vendor =
            serviceManager.vendorResolver().vendorForSysObjectId(sysObjectId);

        const bool ok = db.exec(
            "INSERT INTO probe_devices "
            "(ip, sys_name, sys_descr, sys_object_id, sys_contact, sys_location, "
            " sys_uptime_ticks, interface_macs, interfaces, if_table, lldp_neighbors, "
            " arp_entries, snmp_vendor) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8::jsonb, $9::jsonb, $10::jsonb, "
            " $11::jsonb, $12::jsonb, $13) "
            "ON CONFLICT (ip) DO UPDATE SET "
            "  sys_name = EXCLUDED.sys_name, sys_descr = EXCLUDED.sys_descr, "
            "  sys_object_id = EXCLUDED.sys_object_id, sys_contact = EXCLUDED.sys_contact, "
            "  sys_location = EXCLUDED.sys_location, "
            "  sys_uptime_ticks = EXCLUDED.sys_uptime_ticks, "
            "  interface_macs = EXCLUDED.interface_macs, "
            "  interfaces = EXCLUDED.interfaces, if_table = EXCLUDED.if_table, "
            "  lldp_neighbors = EXCLUDED.lldp_neighbors, arp_entries = EXCLUDED.arp_entries, "
            "  snmp_vendor = EXCLUDED.snmp_vendor, updated_at = now()",
            {ip,
             d.value("sys_name", ""),
             d.value("sys_descr", ""),
             sysObjectId,
             d.value("sys_contact", ""),
             d.value("sys_location", ""),
             std::to_string(d.value("sys_up_time_ticks", 0u)),
             macs,
             ifaces,
             ifTable,
             neighbors,
             arp,
             vendor});
        if (ok)
            ++written;
    }

    LOG_INFO("scan complete, persisted to probe_devices (written={})", written);
}

} // namespace pz::engined
