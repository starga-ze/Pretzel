#include "service/heartbeat/HeartbeatService.h"

#include "service/MgmtdServiceManager.h"

#include "ipc/IpcMessage.h"
#include "util/Logger.h"

namespace nf::mgmtd
{

void HeartbeatService::handleEvent(MgmtdServiceManager& serviceManager,
                                   const HeartbeatEvent& event)
{
    (void)serviceManager;

    switch (event.type())
    {
    case HeartbeatEventType::ReceiveHeartbeatResult:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("HeartbeatService: ReceiveHeartbeatResult has empty message");
            return;
        }

        const auto& payload = msg->getPayload();
        if (payload.empty())
        {
            LOG_WARN("HeartbeatService: HeartbeatResult payload is empty");
            return;
        }

        m_latestJson = std::string(
            reinterpret_cast<const char*>(payload.data()),
            payload.size());

        m_hasData.store(true, std::memory_order_relaxed);

        LOG_DEBUG("HeartbeatService: updated heartbeat result len={}", m_latestJson.size());
        break;
    }

    default:
        LOG_WARN("HeartbeatService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

std::string HeartbeatService::latestJson() const
{
    return m_latestJson;
}

bool HeartbeatService::hasData() const
{
    return m_hasData.load(std::memory_order_relaxed);
}

} // namespace nf::mgmtd
