#pragma once

#include "event/EventFactory.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace nf::icmpd
{

class IcmpdEventFactory final : public nf::event::EventFactory
{
public:
    IcmpdEventFactory() = default;
    ~IcmpdEventFactory() override = default;

    std::unique_ptr<nf::event::Event> create(std::unique_ptr<nf::ipc::IpcMessage> message) override;
    std::unique_ptr<nf::event::Event> create() override;
};

} // namespace nf::icmpd
