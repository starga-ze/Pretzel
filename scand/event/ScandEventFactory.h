#pragma once

#include "event/EventFactory.h"
#include "event/ScandEvent.h"

#include <memory>

namespace pz::scand
{

class ScandEventFactory final : public pz::event::EventFactory<ScandEvent, ScandEventDomain>
{
public:
    ScandEventFactory() = default;
    ~ScandEventFactory() override = default;

    std::unique_ptr<ScandEvent> create() override;
    std::unique_ptr<ScandEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<ScandEvent> create(ScandEventDomain domain, std::uint32_t type) override;
};

}
