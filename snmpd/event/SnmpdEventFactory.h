#pragma once

#include "event/EventFactory.h"
#include "event/SnmpdEvent.h"

#include <memory>

namespace pz::snmpd
{

class SnmpdEventFactory final : public pz::event::EventFactory<SnmpdEvent, SnmpdEventDomain>
{
public:
    SnmpdEventFactory() = default;
    ~SnmpdEventFactory() override = default;

    std::unique_ptr<SnmpdEvent> create() override;
    std::unique_ptr<SnmpdEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<SnmpdEvent> create(SnmpdEventDomain domain, std::uint32_t type) override;
};

} // namespace pz::snmpd
