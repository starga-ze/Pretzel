#pragma once

#include "event/EventFactory.h"
#include "event/ProbedEvent.h"
#include "icmp/IcmpPacket.h"

#include <memory>

namespace pz::probed
{

class ProbedEventFactory final : public pz::event::EventFactory<ProbedEvent, ProbedEventDomain>
{
public:
    ProbedEventFactory() = default;
    ~ProbedEventFactory() override = default;

    std::unique_ptr<ProbedEvent> create(ProbedEventDomain domain, std::uint32_t type) override;
    std::unique_ptr<ProbedEvent> create(std::unique_ptr<pz::ipc::IpcMessage> message) override;
    std::unique_ptr<ProbedEvent> create(const std::string& srcIp, std::unique_ptr<IcmpPacket> packet);

    std::unique_ptr<ProbedEvent> create() override;
};

}
