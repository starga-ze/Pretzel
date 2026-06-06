#pragma once

#include "event/EventFactory.h"
#include "event/TopologydEvent.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace nf::topologyd
{

class TopologydEventFactory : public nf::event::EventFactory<TopologydEvent, TopologydEventDomain>
{
public:
    std::unique_ptr<TopologydEvent> create() override;
    std::unique_ptr<TopologydEvent> create(TopologydEventDomain domain, std::uint32_t type) override;
    std::unique_ptr<TopologydEvent> create(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
};

} // namespace nf::topologyd
