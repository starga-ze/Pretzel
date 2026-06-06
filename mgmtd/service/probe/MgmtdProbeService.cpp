#include "service/probe/MgmtdProbeService.h"
#include "service/MgmtdServiceManager.h"

#include "util/Logger.h"

#include <arpa/inet.h>
#include <cstring>

namespace nf::mgmtd
{

void MgmtdProbeService::handleEvent(MgmtdServiceManager& serviceManager,
                                    const MgmtdProbeEvent& event)
{
    switch (event.type())
    {
    case MgmtdProbeEventType::ReceiveProbeResult:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("MgmtdProbeService: ReceiveProbeResult has empty message");
            return;
        }

        const auto& payload = msg->getPayload();

        if (payload.size() < sizeof(std::uint32_t))
        {
            LOG_WARN("MgmtdProbeService: ProbeResult payload too short size={}", payload.size());
            return;
        }

        std::uint32_t aliveNet = 0;
        std::memcpy(&aliveNet, payload.data(), sizeof(aliveNet));
        const std::uint32_t aliveCount = ntohl(aliveNet);

        LOG_INFO("MgmtdProbeService: ProbeResult alive={}", aliveCount);

        serviceManager.setAliveDevices(aliveCount);
        break;
    }

    default:
        LOG_WARN("MgmtdProbeService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

} // namespace nf::mgmtd
