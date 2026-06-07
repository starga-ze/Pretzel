#pragma once

#include "event/EventFactory.h"
#include "event/MgmtdEvent.h"

#include <memory>

namespace pz::mgmtd
{

class MgmtdEventFactory final : public pz::event::EventFactory<MgmtdEvent, MgmtdEventDomain>
{
public:
    MgmtdEventFactory() = default;
    ~MgmtdEventFactory() override = default;

    std::unique_ptr<MgmtdEvent> create() override;
    std::unique_ptr<MgmtdEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<MgmtdEvent> create(MgmtdEventDomain domain, std::uint32_t type) override;
};

} // namespace pz::mgmtd
