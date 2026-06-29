#include "service/scan/ScanService.h"
#include "service/scan/ScanAction.h"
#include "service/ScandServiceManager.h"

#include "api/ApiTypes.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "config/Config.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <map>
#include <set>

namespace pz::scand
{

namespace
{

nlohmann::json deviceToJson(const SnmpDevice& dev)
{
    nlohmann::json ifaces = nlohmann::json::array();
    for (const auto& itf : dev.interfaces)
    {
        ifaces.push_back({
            {"ip",       itf.ip},
            {"netmask",  itf.netmask},
            {"if_index", itf.ifIndex},
            {"if_name",  itf.ifName},
        });
    }

    nlohmann::json ifTable = nlohmann::json::array();
    for (const auto& e : dev.ifTable)
    {
        ifTable.push_back({
            {"if_index",    e.ifIndex},
            {"name",        e.name},
            {"descr",       e.descr},
            {"alias",       e.alias},
            {"type",        e.type},
            {"speed_mbps",  e.speed},
            {"oper_status", e.operStatus},
            {"mac",         e.mac},
        });
    }

    nlohmann::json neighbors = nlohmann::json::array();
    for (const auto& n : dev.lldpNeighbors)
    {
        neighbors.push_back({
            {"local_port",       n.localPort},
            {"local_port_name",  n.localPortName},
            {"remote_sys_name",  n.remoteSysName},
            {"remote_sys_descr", n.remoteSysDescr},
            {"remote_port_id",   n.remotePortId},
            {"remote_chassis_id",n.remoteChassisId},
        });
    }

    nlohmann::json arp = nlohmann::json::array();
    for (const auto& a : dev.arpEntries)
    {
        arp.push_back({
            {"ip",       a.ip},
            {"mac",      a.mac},
            {"if_index", a.ifIndex},
        });
    }

    return {
        {"ip",                dev.ip},
        {"sys_name",          dev.sysName},
        {"sys_descr",         dev.sysDescr},
        {"sys_object_id",     dev.sysObjectId},
        {"sys_contact",       dev.sysContact},
        {"sys_location",      dev.sysLocation},
        {"sys_up_time_ticks", dev.sysUpTimeTicks},
        {"interface_macs",    dev.interfaceMacs},
        {"interfaces",        std::move(ifaces)},
        {"if_table",          std::move(ifTable)},
        {"lldp_neighbors",    std::move(neighbors)},
        {"arp_entries",       std::move(arp)},
    };
}

// A device row carries an optional "enabled" flag (default true for backward
// compatibility); disabled rows are ignored across every method.
bool rowEnabled(const nlohmann::json& d)
{
    return d.value("enabled", true);
}

// SNMPv3 (USM) credentials — PER-IP ONLY. A host with no entry here ends at v2c
// (no global default fallback). Parses one v3 credential object.
SnmpV3Config parseV3(const nlohmann::json& v)
{
    SnmpV3Config c;
    c.user         = v.value("user",          std::string());
    c.authProtocol = v.value("auth_protocol", std::string("SHA"));
    c.authPassword = v.value("auth_password", std::string());
    c.privProtocol = v.value("priv_protocol", std::string("AES"));
    c.privPassword = v.value("priv_password", std::string());

    const std::string lvl = v.value("security_level", std::string("authPriv"));
    if (lvl == "noAuthNoPriv")
        c.level = SnmpSecurityLevel::NoAuthNoPriv;
    else if (lvl == "authNoPriv")
        c.level = SnmpSecurityLevel::AuthNoPriv;
    else
        c.level = SnmpSecurityLevel::AuthPriv;
    return c;
}

} // namespace

void ScanService::handleEvent(ScandServiceManager& sm, const ScanEvent& event)
{
    switch (event.type())
    {
    case ScanEventType::ReceiveScanRequest:
        handleScanRequest(sm, event);
        break;

    case ScanEventType::SnmpScanComplete:
        handleSnmpComplete(sm, event.devices());
        break;

    case ScanEventType::ApiScanComplete:
        handleApiComplete(sm, event.devices());
        break;

    default:
        LOG_WARN("unhandled event (type={})",
                 static_cast<uint32_t>(event.type()));
        break;
    }
}

// ── Receive scan request from engined ───────────────────────────────────────────
// Splits the requested IPs by scan method and dispatches each half to its own
// engine. A device has exactly one method (chosen in the GUI), so the v2c/v3 set
// (SnmpEngine) and the api set (ApiEngine) are disjoint.
void ScanService::handleScanRequest(ScandServiceManager& sm, const ScanEvent& event)
{
    const auto* msg = event.message();
    if (!msg)
    {
        LOG_WARN("ReceiveScanRequest has empty message");
        return;
    }

    const auto& payload = msg->getPayload();
    if (payload.empty())
    {
        LOG_WARN("ScanRequest payload is empty");
        return;
    }

    std::vector<std::string> ips;
    try
    {
        const std::string json(reinterpret_cast<const char*>(payload.data()),
                               payload.size());
        const auto root = nlohmann::json::parse(json);
        for (const auto& ip : root.value("ips", nlohmann::json::array()))
            ips.push_back(ip.get<std::string>());
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ScanRequest payload (error={})", e.what());
        return;
    }

    if (ips.empty())
    {
        LOG_DEBUG("ScanRequest has no IPs — skipping");
        return;
    }

    if (m_snmpPending || m_apiPending)
    {
        LOG_WARN("scan already in flight, ignoring new request");
        return;
    }

    // Read scan config (effective: global merged with the scand section).
    const auto& t = pz::config::Config::serviceSection("scand", "scan");
    SnmpScanConfig cfg;
    cfg.community     = t.value("community",      std::string("public"));
    cfg.port          = static_cast<uint16_t>(t.value("port",          161));
    cfg.timeoutMs     = t.value("timeout_sec",    2) * 1000;
    cfg.retries       = t.value("retries",        1);
    cfg.maxConcurrent = t.value("max_concurrent", 10);
    cfg.v2cProbeTimeoutMs = t.value("v2c_probe_timeout_ms", 700);
    cfg.v2cProbeRetries   = t.value("v2c_probe_retries",    0);

    // Per-IP v2c community overrides: [{ "ip": "...", "community": "...", ... }].
    if (t.contains("v2c_devices") && t["v2c_devices"].is_array())
    {
        for (const auto& d : t["v2c_devices"])
        {
            if (!d.is_object() || !rowEnabled(d))
                continue;
            const std::string ip = d.value("ip", std::string());
            if (ip.empty())
                continue;
            const std::string community = d.value("community", std::string());
            if (!community.empty())
                cfg.v2cPerIp[ip] = community;
        }
    }

    // Per-IP credential overrides: [{ "ip": "...", "user": ..., ... }, ...].
    if (t.contains("v3_devices") && t["v3_devices"].is_array())
    {
        for (const auto& d : t["v3_devices"])
        {
            if (!d.is_object() || !rowEnabled(d))
                continue;
            const std::string ip = d.value("ip", std::string());
            if (ip.empty())
                continue;
            cfg.v3PerIp[ip] = parseV3(d);
        }
    }

    // Vendor-API creds — the "api" scan method, owned by ApiService and run on its
    // own engine. Filtered down to the IPs actually in this request, mirroring how
    // cfg.v2cPerIp/v3PerIp are only ever consulted for registered IPs.
    const std::set<std::string> ipSet(ips.begin(), ips.end());
    std::map<std::string, ApiCredential> apiDevices;
    for (auto& [ip, cred] : sm.apiService().loadCredentials())
        if (ipSet.count(ip))
            apiDevices.emplace(ip, std::move(cred));

    std::vector<std::string> snmpIps;
    for (const auto& ip : ips)
        if (cfg.isRegistered(ip))
            snmpIps.push_back(ip);

    LOG_INFO("forwarding scan (snmp_ips={}, v3_devices={}, api_devices={})",
             snmpIps.size(), cfg.v3PerIp.size(), apiDevices.size());

    m_collected.clear();
    m_snmpPending = !snmpIps.empty();
    m_apiPending  = !apiDevices.empty();

    if (!m_snmpPending && !m_apiPending)
    {
        LOG_DEBUG("no registered SNMP/v3/API devices among requested IPs");
        return;
    }

    // Delegate to each engine via TxRouter (non-blocking — engines poll in tick).
    if (m_snmpPending)
        sm.txRouter().handleSnmpPacket(std::move(snmpIps), cfg);
    if (m_apiPending)
        sm.txRouter().handleApiPacket(std::move(apiDevices));
}

// ── Scan completed by SnmpEngine (v2c/v3) ───────────────────────────────────────
void ScanService::handleSnmpComplete(ScandServiceManager& sm, const std::vector<SnmpDevice>& devices)
{
    LOG_DEBUG("SnmpEngine subset complete (responding={})", devices.size());
    m_collected.insert(m_collected.end(), devices.begin(), devices.end());
    m_snmpPending = false;
    finalizeIfReady(sm);
}

// ── Scan completed by ApiEngine (vendor API) ────────────────────────────────────
void ScanService::handleApiComplete(ScandServiceManager& sm, const std::vector<SnmpDevice>& devices)
{
    LOG_DEBUG("ApiEngine subset complete (responding={})", devices.size());
    m_collected.insert(m_collected.end(), devices.begin(), devices.end());
    m_apiPending = false;
    finalizeIfReady(sm);
}

// Sends the merged ScanResult once both engines have reported back — see the
// class-level comment in ScanService.h for why this can't be sent piecemeal.
void ScanService::finalizeIfReady(ScandServiceManager& sm)
{
    if (m_snmpPending || m_apiPending)
        return;

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& dev : m_collected)
        arr.push_back(deviceToJson(dev));

    LOG_INFO("scan complete (responding={})", m_collected.size());
    m_collected.clear();

    nlohmann::json root;
    root["devices"] = std::move(arr);

    sm.postAction(std::make_unique<ScanAction>(
        ScanActionType::SendScanResult, root.dump()));
}

void ScanService::handleAction(ScandServiceManager& sm, const ScanAction& action)
{
    switch (action.type())
    {
    case ScanActionType::SendScanResult:
    {
        LOG_DEBUG("sending ScanResult to engined (len={})",
                  action.resultJson().size());

        const std::string& json = action.resultJson();
        std::vector<uint8_t> payload(json.begin(), json.end());

        // ScanResult goes to engined, the single DB writer, which persists it to the
        // probe_devices table. mgmtd reads that table for /api/devices.
        auto header = pz::ipc::IpcHeader::build(
            pz::ipc::IpcDaemon::Scand,
            pz::ipc::IpcDaemon::Engined,
            pz::ipc::IpcCmd::ScanResult,
            static_cast<uint32_t>(payload.size()),
            pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::None));

        auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header),
                                                          std::move(payload));

        sm.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    default:
        LOG_WARN("unhandled action (type={})",
                 static_cast<uint32_t>(action.type()));
        break;
    }
}

} // namespace pz::scand
