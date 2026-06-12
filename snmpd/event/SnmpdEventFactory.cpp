#include "event/SnmpdEventFactory.h"

#include "service/bootstrap/BootstrapEvent.h"
#include "service/heartbeat/HeartbeatEvent.h"
#include "service/scan/ScanEvent.h"
#include "service/reload/ReloadEvent.h"

#include "snmp/SnmpdPacket.h"

#include "util/Logger.h"

namespace pz::snmpd
{

std::unique_ptr<SnmpdEvent> SnmpdEventFactory::create()
{
    return nullptr;
}

std::unique_ptr<SnmpdEvent> SnmpdEventFactory::create(SnmpdEventDomain domain, std::uint32_t type)
{
    switch (domain)
    {
    case SnmpdEventDomain::Bootstrap:
        return std::make_unique<BootstrapEvent>(static_cast<BootstrapEventType>(type));

    case SnmpdEventDomain::Heartbeat:
        return std::make_unique<HeartbeatEvent>(static_cast<HeartbeatEventType>(type));

    case SnmpdEventDomain::Scan:
        return std::make_unique<ScanEvent>(static_cast<ScanEventType>(type));

    case SnmpdEventDomain::Reload:
        return std::make_unique<ReloadEvent>(static_cast<ReloadEventType>(type));

    default:
        LOG_WARN("unhandled domain={}", static_cast<std::uint32_t>(domain));
        return nullptr;
    }
}

std::unique_ptr<SnmpdEvent> SnmpdEventFactory::create(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_DEBUG("Snmpd event factory: received empty message — skipping");
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

    case pz::ipc::IpcCmd::SnmpScanRequest:
        return std::make_unique<ScanEvent>(ScanEventType::ReceiveSnmpScanRequest, std::move(msg));

    case pz::ipc::IpcCmd::ConfigReload:
        return std::make_unique<ReloadEvent>(ReloadEventType::ReceiveConfigReload);

    default:
        LOG_WARN("unhandled cmd={}", static_cast<int>(msg->getCmd()));
        return nullptr;
    }
}

std::unique_ptr<SnmpdEvent> SnmpdEventFactory::create(std::unique_ptr<SnmpdPacket> packet)
{
    if (!packet)
    {
        LOG_WARN("Snmpd event factory: null scan packet — skipping");
        return nullptr;
    }

    return std::make_unique<ScanEvent>(ScanEventType::ScanComplete,
                                       std::move(packet->devices()));
}

} // namespace pz::snmpd
