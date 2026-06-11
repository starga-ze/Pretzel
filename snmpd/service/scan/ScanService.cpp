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
        cfg.v3Fallback    = t.value("v3_fallback",    true);

        // SNMPv3 (USM) credentials — used on v2c-timeout fallback.
        if (t.contains("v3") && t["v3"].is_object())
        {
            const auto& v = t["v3"];
            cfg.v3.user         = v.value("user",          std::string());
            cfg.v3.authProtocol = v.value("auth_protocol", std::string("SHA"));
            cfg.v3.authPassword = v.value("auth_password", std::string());
            cfg.v3.privProtocol = v.value("priv_protocol", std::string("AES"));
            cfg.v3.privPassword = v.value("priv_password", std::string());

            const std::string lvl = v.value("security_level", std::string("authPriv"));
            if (lvl == "noAuthNoPriv")
                cfg.v3.level = SnmpSecurityLevel::NoAuthNoPriv;
            else if (lvl == "authNoPriv")
                cfg.v3.level = SnmpSecurityLevel::AuthNoPriv;
            else
                cfg.v3.level = SnmpSecurityLevel::AuthPriv;
        }

        LOG_INFO("ScanService: forwarding scan to SnmpEngine ips={} v3_fallback={}",
                 ips.size(), cfg.v3Fallback);

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
