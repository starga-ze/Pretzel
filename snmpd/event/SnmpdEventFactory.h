#pragma once

#include "event/EventFactory.h"
#include "event/SnmpdEvent.h"

#include <memory>

namespace nf::snmpd
{

class SnmpdEventFactory final : public nf::event::EventFactory<SnmpdEvent, SnmpdEventDomain>
{
public:
    SnmpdEventFactory() = default;
    ~SnmpdEventFactory() override = default;

    std::unique_ptr<SnmpdEvent> create() override;
    std::unique_ptr<SnmpdEvent> create(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
    std::unique_ptr<SnmpdEvent> create(SnmpdEventDomain domain, std::uint32_t type) override;
};

} // namespace nf::snmpd
