#pragma once

#include "event/EventFactory.h"
#include "event/AuthdEvent.h"

#include <memory>

namespace nf::authd
{

class AuthdEventFactory final : public nf::event::EventFactory<AuthdEvent, AuthdEventDomain>
{
public:
    AuthdEventFactory() = default;
    ~AuthdEventFactory() override = default;

    std::unique_ptr<AuthdEvent> create() override;
    std::unique_ptr<AuthdEvent> create(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
    std::unique_ptr<AuthdEvent> create(AuthdEventDomain domain, std::uint32_t type) override;
};

} // namespace nf::authd
