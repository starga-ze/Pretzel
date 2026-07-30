#include "event/CollectordEventFactory.h"

#include "service/api/ApiEvent.h"
#include "service/bootstrap/BootstrapEvent.h"
#include "service/heartbeat/HeartbeatEvent.h"
#include "service/reload/ReloadEvent.h"

#include "util/Logger.h"

namespace pz::collectord
{

std::unique_ptr<CollectordEvent> CollectordEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<CollectordEvent> CollectordEventFactory::create(CollectordEventDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case CollectordEventDomain::Bootstrap:
        return std::make_unique<BootstrapEvent>(static_cast<BootstrapEventType>(type));

    case CollectordEventDomain::Heartbeat:
        return std::make_unique<HeartbeatEvent>(static_cast<HeartbeatEventType>(type));

    case CollectordEventDomain::Reload:
        return std::make_unique<ReloadEvent>(static_cast<ReloadEventType>(type));

    case CollectordEventDomain::Api:
        return std::make_unique<ApiEvent>(static_cast<ApiEventType>(type));

    default:
        LOG_WARN("unhandled domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

std::unique_ptr<CollectordEvent> CollectordEventFactory::create(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_DEBUG("received empty message — skipping");
        return nullptr;
    }

    switch (msg->getCmd())
    {
    case pz::ipc::IpcCmd::ServerHello:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveServerHello, std::move(msg));

    case pz::ipc::IpcCmd::RuntimeStart:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveRuntimeStart, std::move(msg));

    case pz::ipc::IpcCmd::HeartbeatRequest:
        return std::make_unique<HeartbeatEvent>(HeartbeatEventType::ReceiveHeartbeatRequest, std::move(msg));

    case pz::ipc::IpcCmd::ConfigApply:
        return std::make_unique<ReloadEvent>(ReloadEventType::ReceiveConfigReload);

    case pz::ipc::IpcCmd::ApiKeygenRequest:
        return std::make_unique<ApiEvent>(ApiEventType::RunKeygenTest, std::move(msg));

    case pz::ipc::IpcCmd::ApiEndpointTestRequest:
        return std::make_unique<ApiEvent>(ApiEventType::RunEndpointTest, std::move(msg));

    case pz::ipc::IpcCmd::ApiSaseTestRequest:
        return std::make_unique<ApiEvent>(ApiEventType::RunSaseTest, std::move(msg));

    case pz::ipc::IpcCmd::ApiCredentialStateResponse:
        return std::make_unique<ApiEvent>(ApiEventType::ReceiveKeyState, std::move(msg));

    default:
        LOG_WARN("unhandled cmd (cmd={})", static_cast<int>(msg->getCmd()));
        return nullptr;
    }
}

}
