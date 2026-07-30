#include "event/ProbedEventFactory.h"

#include "service/bootstrap/BootstrapEvent.h"
#include "service/heartbeat/HeartbeatEvent.h"
#include "service/status/icmp/IcmpEvent.h"
#include "service/reload/ReloadEvent.h"

#include "util/Logger.h"

namespace pz::probed
{

std::unique_ptr<ProbedEvent> ProbedEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<ProbedEvent> ProbedEventFactory::create(ProbedEventDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case ProbedEventDomain::Bootstrap:
        return std::make_unique<BootstrapEvent>(static_cast<BootstrapEventType>(type));

    case ProbedEventDomain::Icmp:
        return std::make_unique<IcmpEvent>(static_cast<IcmpEventType>(type));

    case ProbedEventDomain::Reload:
        return std::make_unique<ReloadEvent>(static_cast<ReloadEventType>(type));

    default:
        LOG_WARN("unhandled event domain (domain={})", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

std::unique_ptr<ProbedEvent> ProbedEventFactory::create(std::unique_ptr<pz::ipc::IpcMessage> msg)
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

    case pz::ipc::IpcCmd::ProbeRequest:
        return std::make_unique<IcmpEvent>(IcmpEventType::StartProbe);

    case pz::ipc::IpcCmd::ConfigApply:
        return std::make_unique<ReloadEvent>(ReloadEventType::ReceiveConfigReload);

    default:
        LOG_WARN("unhandled cmd (cmd={})", static_cast<int>(msg->getCmd()));
        return nullptr;
    }

    return nullptr;
}

std::unique_ptr<ProbedEvent> ProbedEventFactory::create(const std::string& srcIp, std::unique_ptr<IcmpPacket> packet)
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
        return std::make_unique<IcmpEvent>(IcmpEventType::EchoReply, srcIp, std::move(packet));

    default:
        LOG_WARN("unhandled ICMP type (type={})", static_cast<int>(packet->type()));
        return nullptr;
    }

    return nullptr;
}

}
