#include "service/heartbeat/HeartbeatService.h"

#include "router/ProbedTxRouter.h"
#include "service/ProbedServiceManager.h"

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include "util/Logger.h"

namespace pz::probed
{

void HeartbeatService::handleEvent(ProbedServiceManager& serviceManager, const HeartbeatEvent& event)
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

        auto action = std::make_unique<HeartbeatAction>(HeartbeatActionType::SendHeartbeatResponse, src);

        serviceManager.postAction(std::move(action));
        break;
    }

    default:
        LOG_WARN("unhandled event (type={})", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void HeartbeatService::handleAction(ProbedServiceManager& serviceManager, const HeartbeatAction& action)
{
    switch (action.type())
    {
    case HeartbeatActionType::SendHeartbeatResponse:
    {
        auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response);

        pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(pz::ipc::IpcDaemon::Probed, action.dst(),
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

}
