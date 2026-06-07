#pragma once

#include "event/EventFactory.h"
#include "event/AuthdEvent.h"

#include <memory>

namespace pz::authd
{

class AuthdEventFactory final : public pz::event::EventFactory<AuthdEvent, AuthdEventDomain>
{
public:
    AuthdEventFactory() = default;
    ~AuthdEventFactory() override = default;

    std::unique_ptr<AuthdEvent> create() override;
    std::unique_ptr<AuthdEvent> create(std::unique_ptr<pz::ipc::IpcMessage> msg) override;
    std::unique_ptr<AuthdEvent> create(AuthdEventDomain domain, std::uint32_t type) override;
};

} // namespace pz::authd
