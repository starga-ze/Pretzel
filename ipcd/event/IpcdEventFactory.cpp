#include "event/IpcdEventFactory.h"

#include "service/bootstrap/IpcdBootstrapEvent.h"

#include "util/Logger.h"

namespace nf::ipcd
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
        return std::make_unique<IpcdBootstrapEvent>(static_cast<IpcdBootstrapEventType>(type));

    default:
        LOG_WARN("IpcdEventFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

std::unique_ptr<IpcdEvent> IpcdEventFactory::create(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("IpcdEventFactory: null message");
        return nullptr;
    }

    switch (msg->getCmd())
    {
    case nf::ipc::IpcCmd::ClientHello:
        return std::make_unique<IpcdBootstrapEvent>(IpcdBootstrapEventType::ReceiveClientHello, std::move(msg));

    case nf::ipc::IpcCmd::SyncRequest:
        return std::make_unique<IpcdBootstrapEvent>(IpcdBootstrapEventType::ReceiveSyncRequest, std::move(msg));

    case nf::ipc::IpcCmd::RuntimeReady:
        return std::make_unique<IpcdBootstrapEvent>(IpcdBootstrapEventType::ReceiveRuntimeReady, std::move(msg));

    default:
        return nullptr;
    }
}

} // namespace nf::ipcd
