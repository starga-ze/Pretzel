#include "service/scan/ScanService.h"
#include "service/scan/ScanAction.h"
#include "service/SnmpdServiceManager.h"

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "config/Config.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::snmpd
{

void ScanService::handleEvent(SnmpdServiceManager& sm, const ScanEvent& event)
{
    switch (event.type())
    {
    // ── Receive scan request from mgmtd ──────────────────────────────────────
    case ScanEventType::ReceiveSnmpScanRequest:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("ScanService: ReceiveSnmpScanRequest has empty message");
            return;
        }

        const auto& payload = msg->getPayload();
        if (payload.empty())
        {
            LOG_WARN("ScanService: SnmpScanRequest payload is empty");
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
            LOG_WARN("ScanService: failed to parse SnmpScanRequest payload: {}", e.what());
            return;
        }

        if (ips.empty())
        {
            LOG_DEBUG("ScanService: SnmpScanRequest has no IPs — skipping");
            return;
        }

        // Read scan config (effective: global merged with the snmpd section).
        const auto& t = pz::config::Config::serviceSection("snmpd", "scan");
        SnmpScanConfig cfg;
        cfg.community     = t.value("community",      std::string("public"));
        cfg.port          = static_cast<uint16_t>(t.value("port",          161));
        cfg.timeoutMs     = t.value("timeout_sec",    2) * 1000;
        cfg.retries       = t.value("retries",        1);
        cfg.maxConcurrent = t.value("max_concurrent", 10);
        cfg.v2cProbeTimeoutMs = t.value("v2c_probe_timeout_ms", 700);
        cfg.v2cProbeRetries   = t.value("v2c_probe_retries",    0);

        // SNMPv3 (USM) credentials — PER-IP ONLY. A host with no entry here ends at
        // v2c (no global default fallback). Parses one v3 credential object.
        auto parseV3 = [](const nlohmann::json& v) {
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
        };

        // Per-IP credential overrides: [{ "ip": "...", "user": ..., ... }, ...].
        if (t.contains("v3_devices") && t["v3_devices"].is_array())
        {
            for (const auto& d : t["v3_devices"])
            {
                if (!d.is_object())
                    continue;
                const std::string ip = d.value("ip", std::string());
                if (ip.empty())
                    continue;
                cfg.v3PerIp[ip] = parseV3(d);
            }
        }

        LOG_INFO("ScanService: forwarding scan to SnmpEngine ips={} v3_devices={}",
                 ips.size(), cfg.v3PerIp.size());

        // Delegate to SnmpEngine via TxRouter (non-blocking — engine polls in tick)
        sm.txRouter().startSnmpScan(std::move(ips), std::move(cfg));
        break;
    }

    // ── Scan completed by SnmpEngine ─────────────────────────────────────────
    case ScanEventType::ScanComplete:
    {
        const auto& devices = event.devices();

        LOG_INFO("ScanService: ScanComplete responding={}", devices.size());

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& dev : devices)
        {
            arr.push_back({
                {"ip",               dev.ip},
                {"sys_name",         dev.sysName},
                {"sys_descr",        dev.sysDescr},
                {"sys_object_id",    dev.sysObjectId},
                {"sys_contact",      dev.sysContact},
                {"sys_location",     dev.sysLocation},
                {"sys_up_time_ticks", dev.sysUpTimeTicks},
            });
        }

        nlohmann::json root;
        root["devices"] = std::move(arr);

        sm.postAction(std::make_unique<ScanAction>(
            ScanActionType::SendSnmpResult, root.dump()));
        break;
    }

    default:
        LOG_WARN("ScanService: unhandled event type={}",
                 static_cast<uint32_t>(event.type()));
        break;
    }
}

void ScanService::handleAction(SnmpdServiceManager& sm, const ScanAction& action)
{
    switch (action.type())
    {
    case ScanActionType::SendSnmpResult:
    {
        LOG_DEBUG("ScanService: sending SnmpResult to mgmtd len={}",
                  action.resultJson().size());

        const std::string& json = action.resultJson();
        std::vector<uint8_t> payload(json.begin(), json.end());

        auto header = pz::ipc::IpcHeader::build(
            pz::ipc::IpcDaemon::Snmpd,
            pz::ipc::IpcDaemon::Mgmtd,
            pz::ipc::IpcCmd::SnmpResult,
            static_cast<uint32_t>(payload.size()),
            pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::None));

        auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header),
                                                          std::move(payload));

        sm.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    default:
        LOG_WARN("ScanService: unhandled action type={}",
                 static_cast<uint32_t>(action.type()));
        break;
    }
}

} // namespace pz::snmpd
