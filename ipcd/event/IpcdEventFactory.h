#pragma once

#include "event/EventFactory.h"
#include "event/IpcdEvent.h"

#include <memory>

namespace nf::ipcd
{

class IpcdEventFactory final : public nf::event::EventFactory<IpcdEvent, IpcdEventDomain>
{
public:
    IpcdEventFactory() = default;
    ~IpcdEventFactory() override = default;

    std::unique_ptr<IpcdEvent> create() override;
    std::unique_ptr<IpcdEvent> create(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
    std::unique_ptr<IpcdEvent> create(IpcdEventDomain domain, std::uint32_t type) override;
};

} // namespace nf::ipcd
