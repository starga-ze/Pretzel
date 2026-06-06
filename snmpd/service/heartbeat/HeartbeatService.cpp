#include "service/heartbeat/HeartbeatService.h"

#include "service/SnmpdServiceManager.h"
#include "router/SnmpdTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "util/Logger.h"

namespace nf::snmpd
{

void HeartbeatService::handleEvent(SnmpdServiceManager& serviceManager,
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

        nf::ipc::IpcDaemon src = msg->getSrc();

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

void HeartbeatService::handleAction(SnmpdServiceManager& serviceManager,
                                    const HeartbeatAction& action)
{
    switch (action.type())
    {
    case HeartbeatActionType::SendHeartbeatResponse:
    {
        auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Response);

        nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Snmpd,
            action.dst(),
            nf::ipc::IpcCmd::HeartbeatResponse,
            0,
            flag);

        auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));

        LOG_DEBUG("HeartbeatService: Tx HeartbeatResponse dst={}",
                  nf::ipc::IpcProtocol::daemonToStr(action.dst()));

        serviceManager.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    default:
        LOG_WARN("HeartbeatService: unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        break;
    }
}

} // namespace nf::snmpd
