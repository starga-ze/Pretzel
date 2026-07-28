#pragma once

#include "event/EventFactory.h"
#include "event/CollectordEvent.h"

#include <memory>

namespace pz::collectord
{

class CollectordEventFactory final : public pz::event::EventFactory<CollectordEvent, CollectordEventDomain>
{
public:
    CollectordEventFactory() = default;
    ~CollectordEventFactory() override = default;

    std::unique_ptr<CollectordEvent> create() override;
    std::unique_ptr<CollectordEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<CollectordEvent> create(CollectordEventDomain domain, std::uint32_t type) override;
};

}
