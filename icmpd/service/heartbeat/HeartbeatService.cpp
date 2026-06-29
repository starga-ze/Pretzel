#include "service/heartbeat/HeartbeatService.h"

#include "service/IcmpdServiceManager.h"
#include "router/IcmpdTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "util/Logger.h"

namespace pz::icmpd
{

void HeartbeatService::handleEvent(IcmpdServiceManager& serviceManager,
                                   const HeartbeatEvent& event)
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

        auto action = std::make_unique<HeartbeatAction>(
            HeartbeatActionType::SendHeartbeatResponse,
            src);

        serviceManager.postAction(std::move(action));
        break;
    }

    default:
        LOG_WARN("unhandled event (type={})",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void HeartbeatService::handleAction(IcmpdServiceManager& serviceManager,
                                    const HeartbeatAction& action)
{
    switch (action.type())
    {
    case HeartbeatActionType::SendHeartbeatResponse:
    {
        auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response);

        pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
            pz::ipc::IpcDaemon::Icmpd,
            action.dst(),
            pz::ipc::IpcCmd::HeartbeatResponse,
            0,
            flag);

        auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));

        LOG_TRACE("Tx HeartbeatResponse (dst={})",
                  pz::ipc::IpcProtocol::daemonToStr(action.dst()));

        serviceManager.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    default:
        LOG_WARN("unhandled action (type={})",
                 static_cast<std::uint32_t>(action.type()));
        break;
    }
}

} // namespace pz::icmpd
