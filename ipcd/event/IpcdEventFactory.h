#pragma once

#include "event/EventFactory.h"
#include "event/IpcdEvent.h"

#include <memory>

namespace pz::ipcd
{

class IpcdEventFactory final : public pz::event::EventFactory<IpcdEvent, IpcdEventDomain>
{
public:
    IpcdEventFactory() = default;
    ~IpcdEventFactory() override = default;

    std::unique_ptr<IpcdEvent> create() override;
    std::unique_ptr<IpcdEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<IpcdEvent> create(IpcdEventDomain domain, std::uint32_t type) override;
};

}
