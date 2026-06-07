#include "event/IpcdEventFactory.h"

#include "service/bootstrap/BootstrapEvent.h"

#include "util/Logger.h"

namespace pz::ipcd
{

std::unique_ptr<IpcdEvent> IpcdEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<IpcdEvent> IpcdEventFactory::create(IpcdEventDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case IpcdEventDomain::Bootstrap:
        return std::make_unique<BootstrapEvent>(static_cast<BootstrapEventType>(type));

    default:
        LOG_WARN("IpcdEventFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

std::unique_ptr<IpcdEvent> IpcdEventFactory::create(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("IpcdEventFactory: null message");
        return nullptr;
    }

    switch (msg->getCmd())
    {
    case pz::ipc::IpcCmd::ClientHello:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveClientHello, std::move(msg));

    case pz::ipc::IpcCmd::SyncRequest:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveSyncRequest, std::move(msg));

    case pz::ipc::IpcCmd::RuntimeReady:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveRuntimeReady, std::move(msg));

    default:
        return nullptr;
    }
}

} // namespace pz::ipcd
