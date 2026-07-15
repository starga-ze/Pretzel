#pragma once

#include "event/EventFactory.h"
#include "event/IcmpdEvent.h"
#include "icmp/IcmpPacket.h"

#include <memory>

namespace pz::icmpd
{

class IcmpdEventFactory final : public pz::event::EventFactory<IcmpdEvent, IcmpdEventDomain>
{
public:
    IcmpdEventFactory() = default;
    ~IcmpdEventFactory() override = default;

    std::unique_ptr<IcmpdEvent> create(IcmpdEventDomain domain, std::uint32_t type) override;
    std::unique_ptr<IcmpdEvent> create(std::unique_ptr<pz::ipc::IpcMessage> message) override;
    std::unique_ptr<IcmpdEvent> create(const std::string& srcIp, std::unique_ptr<IcmpPacket> packet);

    std::unique_ptr<IcmpdEvent> create() override;
};

}
