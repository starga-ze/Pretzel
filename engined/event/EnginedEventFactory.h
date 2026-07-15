#pragma once

#include "event/EnginedEvent.h"
#include "event/EventFactory.h"

#include <memory>

namespace pz::engined
{

class EnginedEventFactory final : public pz::event::EventFactory<EnginedEvent, EnginedEventDomain>
{
public:
    EnginedEventFactory() = default;
    ~EnginedEventFactory() override = default;

    std::unique_ptr<EnginedEvent> create() override;
    std::unique_ptr<EnginedEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<EnginedEvent> create(EnginedEventDomain domain, std::uint32_t type) override;
};

}
