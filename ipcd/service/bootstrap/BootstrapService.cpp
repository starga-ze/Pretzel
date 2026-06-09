#include "service/bootstrap/BootstrapService.h"

#include "service/IpcdServiceManager.h"
#include "event/IpcdEventFactory.h"
#include "action/IpcdActionFactory.h"
#include "ipc/IpcServerHandler.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::ipcd
{

BootstrapService::BootstrapService(IpcdEventFactory* eventFactory,
                                           IpcdActionFactory* actionFactory,
                                           IpcServerHandler* ipcServerHandler)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory),
      m_ipcServerHandler(ipcServerHandler)
{
}

void BootstrapService::handleEvent(IpcdServiceManager& serviceManager,
                                       const BootstrapEvent& event)
{
    if (!m_actionFactory)
    {
        LOG_ERROR("bootstrap: action factory is not initialized");
        return;
    }

    switch (event.type())
    {
    case BootstrapEventType::ReceiveClientHello:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("bootstrap: received empty ClientHello");
            return;
        }

        LOG_INFO("ReceiveClientHello src={}",
                 pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

        auto action = std::make_unique<BootstrapAction>(
            BootstrapActionType::SendServerHello,
            std::make_unique<pz::ipc::IpcMessage>(*msg));

        serviceManager.postAction(std::move(action));
        break;
    }

    case BootstrapEventType::ReceiveSyncRequest:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("bootstrap: received empty SyncRequest");
            return;
        }

        LOG_INFO("ReceiveSyncRequest src={}",
                 pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

        auto action = std::make_unique<BootstrapAction>(
            BootstrapActionType::SendSyncResponse,
            std::make_unique<pz::ipc::IpcMessage>(*msg));

        serviceManager.postAction(std::move(action));
        break;
    }

    case BootstrapEventType::ReceiveRuntimeReady:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("bootstrap: received empty RuntimeReady");
            return;
        }

        LOG_INFO("ReceiveRuntimeReady src={}",
                 pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

        if (m_ipcServerHandler)
        {
            m_ipcServerHandler->markRuntimeReady(msg->getSrc(), true);
        }

        break;
    }

    default:
        LOG_WARN("unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void BootstrapService::handleAction(IpcdServiceManager& serviceManager,
                                        const BootstrapAction& action)
{
    switch (action.type())
    {
    case BootstrapActionType::SendServerHello:
    {
        const auto* req = action.request();
        if (!req)
        {
            LOG_WARN("bootstrap: SendServerHello — no pending request");
            return;
        }

        auto msg = buildServerHello(*req);
        serviceManager.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    case BootstrapActionType::SendSyncResponse:
    {
        const auto* req = action.request();
        if (!req)
        {
            LOG_WARN("bootstrap: SendSyncResponse — no pending request");
            return;
        }

        auto msg = buildSyncResponse(*req);
        serviceManager.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    default:
        LOG_WARN("unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        break;
    }
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildServerHello(const pz::ipc::IpcMessage& req) const
{
    const std::string name = pz::ipc::IpcProtocol::daemonToStr(pz::ipc::IpcDaemon::Ipcd);

    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Ipcd,
        req.getSrc(),
        pz::ipc::IpcCmd::ServerHello,
        req.getSeqNo(),
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    return msg;
}

std::unique_ptr<pz::ipc::IpcMessage>
BootstrapService::buildSyncResponse(const pz::ipc::IpcMessage& req) const
{
    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Ipcd,
        req.getSrc(),
        pz::ipc::IpcCmd::SyncResponse,
        req.getSeqNo(),
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));

    nlohmann::json payloadJson;
    payloadJson["daemons"] = nlohmann::json::array();

    if (m_ipcServerHandler)
    {
        const auto& runtimeTable = m_ipcServerHandler->getRuntimeTable();

        for (const auto& [daemon, state] : runtimeTable)
        {
            if (daemon == pz::ipc::IpcDaemon::Engined)
            {
                continue;
            }

            if (state.fd < 0)
            {
                continue;
            }

            payloadJson["daemons"].push_back({
                {"daemon",      pz::ipc::IpcProtocol::daemonToStr(daemon)},
                {"ready",       state.ready},
                {"generation",  state.generation}
            });
        }
    }

    const std::string payload = payloadJson.dump();
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());

    return msg;
}

} // namespace pz::ipcd
