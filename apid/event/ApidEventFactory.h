#pragma once

#include "event/EventFactory.h"
#include "event/ApidEvent.h"

#include <memory>

namespace pz::apid
{

class ApidEventFactory final : public pz::event::EventFactory<ApidEvent, ApidEventDomain>
{
public:
    ApidEventFactory() = default;
    ~ApidEventFactory() override = default;

    std::unique_ptr<ApidEvent> create() override;
    std::unique_ptr<ApidEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<ApidEvent> create(ApidEventDomain domain, std::uint32_t type) override;
};

} // namespace pz::apid
