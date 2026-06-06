#include "event/EnginedEventFactory.h"

#include "service/bootstrap/EnginedBootstrapEvent.h"

#include "util/Logger.h"

namespace nf::engined
{

std::unique_ptr<EnginedEvent> EnginedEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<EnginedEvent> EnginedEventFactory::create(EnginedEventDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case EnginedEventDomain::Bootstrap:
        return std::make_unique<EnginedBootstrapEvent>(static_cast<EnginedBootstrapEventType>(type));

    default:
        LOG_WARN("EnginedEventFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

std::unique_ptr<EnginedEvent> EnginedEventFactory::create(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("EnginedEventFactory: null message");
        return nullptr;
    }

    switch (msg->getCmd())
    {
    case nf::ipc::IpcCmd::ServerHello:
        return std::make_unique<EnginedBootstrapEvent>(EnginedBootstrapEventType::ReceiveServerHello, std::move(msg));

    case nf::ipc::IpcCmd::SyncResponse:
        return std::make_unique<EnginedBootstrapEvent>(EnginedBootstrapEventType::ReceiveSyncResponse, std::move(msg));

    case nf::ipc::IpcCmd::RuntimeStart:
        return std::make_unique<EnginedBootstrapEvent>(EnginedBootstrapEventType::ReceiveRuntimeStart, std::move(msg));

    default:
        LOG_WARN("EnginedEventFactory: unhandled cmd={}", static_cast<int>(msg->getCmd()));
        return nullptr;
    }
}

} // namespace nf::engined
