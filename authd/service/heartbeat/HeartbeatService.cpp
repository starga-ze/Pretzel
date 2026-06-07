#include "service/heartbeat/HeartbeatService.h"

#include "service/AuthdServiceManager.h"
#include "router/AuthdTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "util/Logger.h"

namespace pz::authd
{

void HeartbeatService::handleEvent(AuthdServiceManager& serviceManager,
                                   const HeartbeatEvent& event)
{
    switch (event.type())
    {
    case HeartbeatEventType::ReceiveHeartbeatRequest:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("HeartbeatService: ReceiveHeartbeatRequest has empty message");
            return;
        }

        pz::ipc::IpcDaemon src = msg->getSrc();

        auto action = std::make_unique<HeartbeatAction>(
            HeartbeatActionType::SendHeartbeatResponse,
            src);

        serviceManager.postAction(std::move(action));
        break;
    }

    default:
        LOG_WARN("HeartbeatService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void HeartbeatService::handleAction(AuthdServiceManager& serviceManager,
                                    const HeartbeatAction& action)
{
    switch (action.type())
    {
    case HeartbeatActionType::SendHeartbeatResponse:
    {
        auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response);

        pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
            pz::ipc::IpcDaemon::Authd,
            action.dst(),
            pz::ipc::IpcCmd::HeartbeatResponse,
            0,
            flag);

        auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));

        LOG_DEBUG("HeartbeatService: Tx HeartbeatResponse dst={}",
                  pz::ipc::IpcProtocol::daemonToStr(action.dst()));

        serviceManager.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    default:
        LOG_WARN("HeartbeatService: unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        break;
    }
}

} // namespace pz::authd
