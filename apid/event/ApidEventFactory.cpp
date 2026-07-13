#include "event/ApidEventFactory.h"

#include "service/bootstrap/BootstrapEvent.h"

#include "util/Logger.h"

namespace pz::apid
{

std::unique_ptr<ApidEvent> ApidEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<ApidEvent> ApidEventFactory::create(ApidEventDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case ApidEventDomain::Bootstrap:
        return std::make_unique<BootstrapEvent>(static_cast<BootstrapEventType>(type));

    default:
        LOG_WARN("unhandled domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

std::unique_ptr<ApidEvent> ApidEventFactory::create(std::unique_ptr<pz::ipc::IpcMessage> msg)
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

    default:
        LOG_WARN("unhandled cmd (cmd={})", static_cast<int>(msg->getCmd()));
        return nullptr;
    }
}

} // namespace pz::apid
