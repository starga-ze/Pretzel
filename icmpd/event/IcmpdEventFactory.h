#pragma once

#include "event/EventFactory.h"
#include "event/IcmpdEvent.h"

#include <memory>

namespace nf::icmpd
{

class IcmpdEventFactory final : public nf::event::EventFactory<IcmpdEvent, IcmpdEventDomain>
{
public:
    IcmpdEventFactory() = default;
    ~IcmpdEventFactory() override = default;

    std::unique_ptr<IcmpdEvent> create(std::unique_ptr<nf::ipc::IpcMessage> message) override;
    std::unique_ptr<IcmpdEvent> create(IcmpdEventDomain domain, std::uint32_t type) override;
    std::unique_ptr<IcmpdEvent> create() override;
};

} // namespace nf::icmpd
