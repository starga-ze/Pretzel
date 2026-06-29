#pragma once

#include "event/EventFactory.h"
#include "event/ScandEvent.h"

#include <memory>

namespace pz::scand
{

class SnmpPacket;
class ApiPacket;

class ScandEventFactory final : public pz::event::EventFactory<ScandEvent, ScandEventDomain>
{
public:
    ScandEventFactory() = default;
    ~ScandEventFactory() override = default;

    std::unique_ptr<ScandEvent> create() override;
    std::unique_ptr<ScandEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<ScandEvent> create(ScandEventDomain domain, std::uint32_t type) override;

    // Typed scan-result packets → Snmp/ApiScanComplete events (mirrors icmpd's
    // create(srcIp, IcmpPacket)). Keeps the ScanEvent construction out of the router.
    std::unique_ptr<ScandEvent> create(std::unique_ptr<SnmpPacket> packet);
    std::unique_ptr<ScandEvent> create(std::unique_ptr<ApiPacket> packet);
};

} // namespace pz::scand
