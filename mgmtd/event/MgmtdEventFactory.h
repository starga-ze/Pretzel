#pragma once

#include "event/EventFactory.h"
#include "event/MgmtdEvent.h"

#include <memory>

namespace nf::mgmtd
{

class MgmtdEventFactory final : public nf::event::EventFactory<MgmtdEvent, MgmtdEventDomain>
{
public:
    MgmtdEventFactory() = default;
    ~MgmtdEventFactory() override = default;

    std::unique_ptr<MgmtdEvent> create() override;
    std::unique_ptr<MgmtdEvent> create(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
    std::unique_ptr<MgmtdEvent> create(MgmtdEventDomain domain, std::uint32_t type) override;
};

} // namespace nf::mgmtd
