#include "service/probe/ProbeService.h"
#include "service/MgmtdServiceManager.h"

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
        serviceManager.setAliveIps(std::move(ips));
        break;
    }

    default:
        LOG_WARN("unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

} // namespace pz::mgmtd
