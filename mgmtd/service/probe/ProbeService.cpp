#include "service/probe/ProbeService.h"
#include "service/MgmtdServiceManager.h"

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include "util/Logger.h"
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <cstring>

namespace pz::mgmtd
{

void ProbeService::handleEvent(MgmtdServiceManager& serviceManager,
                                    const ProbeEvent& event)
{
    switch (event.type())
    {
    case ProbeEventType::ReceiveProbeResult:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("ReceiveProbeResult has empty message");
            return;
        }

        const auto& payload = msg->getPayload();

        if (payload.size() < sizeof(std::uint32_t))
        {
            LOG_WARN("ProbeResult payload too short size={}", payload.size());
            return;
        }

        // Try JSON payload first {"alive": N, "ips": [...]}; fall back to legacy uint32_t
        std::vector<std::string> ips;
        std::uint32_t aliveCount = 0;

        try
        {
            const std::string jsonStr(reinterpret_cast<const char*>(payload.data()), payload.size());
            const auto root = nlohmann::json::parse(jsonStr);
            aliveCount = root.value("alive", 0u);
            for (const auto& ip : root.value("ips", nlohmann::json::array()))
                ips.push_back(ip.get<std::string>());
        }
        catch (...)
        {
            // legacy: 4-byte big-endian count
            if (payload.size() >= sizeof(std::uint32_t))
            {
                std::uint32_t aliveNet = 0;
                std::memcpy(&aliveNet, payload.data(), sizeof(aliveNet));
                aliveCount = ntohl(aliveNet);
            }
        }

        LOG_INFO("ProbeResult alive={} ips={}", aliveCount, ips.size());

        serviceManager.setAliveDevices(aliveCount);
        serviceManager.setAliveIps(ips);

        // Forward IP list to snmpd for SNMP scanning
        if (!ips.empty())
        {
            nlohmann::json scanReq;
            scanReq["ips"] = ips;
            const std::string scanJson = scanReq.dump();
            std::vector<uint8_t> scanPayload(scanJson.begin(), scanJson.end());

            // SnmpScanRequest goes to engined (the control-plane hub), which relays
            // it to snmpd.
            auto scanHeader = pz::ipc::IpcHeader::build(
                pz::ipc::IpcDaemon::Mgmtd,
                pz::ipc::IpcDaemon::Engined,
                pz::ipc::IpcCmd::SnmpScanRequest,
                static_cast<uint32_t>(scanPayload.size()),
                pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::None));

            auto scanMsg = std::make_unique<pz::ipc::IpcMessage>(
                std::move(scanHeader), std::move(scanPayload));

            LOG_DEBUG("ProbeService: forwarding {} IPs to snmpd for SNMP scan", ips.size());

            serviceManager.txRouter().handleIpcMessage(std::move(scanMsg));
        }
        break;
    }

    default:
        LOG_WARN("unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

} // namespace pz::mgmtd
