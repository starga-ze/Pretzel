#include "event/MgmtdEventFactory.h"

#include "service/bootstrap/BootstrapEvent.h"
#include "service/probe/ProbeEvent.h"
#include "service/heartbeat/HeartbeatEvent.h"

#include "util/Logger.h"

namespace pz::mgmtd
{

std::unique_ptr<MgmtdEvent> MgmtdEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<MgmtdEvent> MgmtdEventFactory::create(MgmtdEventDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case MgmtdEventDomain::Bootstrap:
        return std::make_unique<BootstrapEvent>(static_cast<BootstrapEventType>(type));

    case MgmtdEventDomain::Probe:
        return std::make_unique<ProbeEvent>(static_cast<ProbeEventType>(type));

    case MgmtdEventDomain::Heartbeat:
        return std::make_unique<HeartbeatEvent>(static_cast<HeartbeatEventType>(type));

    default:
        LOG_WARN("MgmtdEventFactory: unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

std::unique_ptr<MgmtdEvent> MgmtdEventFactory::create(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("MgmtdEventFactory: null message");
        return nullptr;
    }

    switch (msg->getCmd())
    {
    case pz::ipc::IpcCmd::ServerHello:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveServerHello, std::move(msg));

    case pz::ipc::IpcCmd::RuntimeStart:
        return std::make_unique<BootstrapEvent>(BootstrapEventType::ReceiveRuntimeStart, std::move(msg));

    case pz::ipc::IpcCmd::ProbeResult:
        return std::make_unique<ProbeEvent>(ProbeEventType::ReceiveProbeResult, std::move(msg));

    case pz::ipc::IpcCmd::HeartbeatRequest:
        return std::make_unique<HeartbeatEvent>(HeartbeatEventType::ReceiveHeartbeatRequest,
                                               std::move(msg));

    case pz::ipc::IpcCmd::HeartbeatResult:
        return std::make_unique<HeartbeatEvent>(HeartbeatEventType::ReceiveHeartbeatResult,
                                               std::move(msg));

    default:
        LOG_WARN("MgmtdEventFactory: unhandled cmd={}", static_cast<int>(msg->getCmd()));
        return nullptr;
    }
}

} // namespace pz::mgmtd
