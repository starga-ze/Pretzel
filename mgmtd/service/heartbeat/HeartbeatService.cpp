#include "service/heartbeat/HeartbeatService.h"

#include "router/MgmtdTxRouter.h"
#include "service/MgmtdServiceManager.h"

#include "config/Config.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::mgmtd
{

void HeartbeatService::handleEvent(MgmtdServiceManager& serviceManager, const HeartbeatEvent& event)
{
    switch (event.type())
    {
    case HeartbeatEventType::ReceiveHeartbeatRequest:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("received empty heartbeat request");
            return;
        }

        pz::ipc::IpcDaemon src = msg->getSrc();

        serviceManager.postAction(std::make_unique<HeartbeatAction>(HeartbeatActionType::SendHeartbeatResponse, src));
        break;
    }

    case HeartbeatEventType::ReceiveHeartbeatResult:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("ReceiveHeartbeatResult has empty message");
            return;
        }

        const auto& payload = msg->getPayload();
        if (payload.empty())
        {
            LOG_WARN("HeartbeatResult payload is empty");
            return;
        }

        m_latestJson = std::string(reinterpret_cast<const char*>(payload.data()), payload.size());

        m_hasData.store(true, std::memory_order_relaxed);

        LOG_TRACE("updated heartbeat result (len={})", m_latestJson.size());

        break;
    }

    default:
        LOG_WARN("unhandled event (type={})", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void HeartbeatService::handleAction(MgmtdServiceManager& serviceManager, const HeartbeatAction& action)
{
    switch (action.type())
    {
    case HeartbeatActionType::SendHeartbeatResponse:
    {
        const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response);

        pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(pz::ipc::IpcDaemon::Mgmtd, action.dst(),
                                                              pz::ipc::IpcCmd::HeartbeatResponse, 0, flag);

        auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));

        LOG_TRACE("Tx HeartbeatResponse (dst={})", pz::ipc::IpcProtocol::daemonToStr(action.dst()));

        serviceManager.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    default:
        LOG_WARN("unhandled action (type={})", static_cast<std::uint32_t>(action.type()));
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

}
