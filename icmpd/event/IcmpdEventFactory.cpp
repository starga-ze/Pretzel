#include "event/IcmpdEventFactory.h"

#include "service/bootstrap/BootstrapEvent.h"
#include "service/probe/ProbeEvent.h"

#include "util/Logger.h"

namespace nf::icmpd
{

std::unique_ptr<IcmpdEvent> IcmpdEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<IcmpdEvent> IcmpdEventFactory::create(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("IcmpdEventFactory: null message");
        return nullptr;
    }

    switch (msg->getCmd())
    {
    case nf::ipc::IpcCmd::ServerHello:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveServerHello, std::move(msg));

    case nf::ipc::IpcCmd::RuntimeStart:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveRuntimeStart, std::move(msg));
    
    default:
        LOG_WARN("IcmpdEventFactory: unhandled cmd={}", static_cast<int>(msg->getCmd()));
        return nullptr;
    }

    return nullptr;
}

std::unique_ptr<IcmpdEvent> IcmpdEventFactory::create(IcmpdEventDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case IcmpdEventDomain::Bootstrap:
        return std::make_unique<BootstrapEvent>(static_cast<BootstrapEventType>(type));

    case IcmpdEventDomain::Probe:
        return std::make_unique<ProbeEvent>(static_cast<ProbeEventType>(type));

    default:
        LOG_WARN("Unhandled event domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

} // namespace nf::icmpd
