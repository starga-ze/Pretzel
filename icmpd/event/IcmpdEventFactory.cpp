#include "event/IcmpdEventFactory.h"

#include "service/bootstrap/BootstrapEvent.h"
#include "service/probe/ProbeEvent.h"
#include "service/heartbeat/HeartbeatEvent.h"

#include "util/Logger.h"

namespace pz::icmpd
{

std::unique_ptr<IcmpdEvent> IcmpdEventFactory::create()
{
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

std::unique_ptr<IcmpdEvent> IcmpdEventFactory::create(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_DEBUG("Icmpd event factory: received empty message — skipping");
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

    default:
        LOG_WARN("unhandled cmd={}", static_cast<int>(msg->getCmd()));
        return nullptr;
    }

    return nullptr;
}

std::unique_ptr<IcmpdEvent> IcmpdEventFactory::create(const std::string& srcIp, 
        std::unique_ptr<IcmpPacket> packet)
{
    if (!packet)
    {
        LOG_WARN("null packet");
        return nullptr;
    }

    switch (packet->type())
    {
    case IcmpType::EchoRequest:
    case IcmpType::DestinationUnreachable:
    case IcmpType::Redirect:
    case IcmpType::TimeExceeded:
        return nullptr;

    case IcmpType::EchoReply:
        return std::make_unique<ProbeEvent>(ProbeEventType::EchoReply, srcIp, std::move(packet));

    default:
        LOG_WARN("Unhandled type={}", static_cast<int>(packet->type()));
        return nullptr;
    }

    return nullptr;
}

} // namespace pz::icmpd
