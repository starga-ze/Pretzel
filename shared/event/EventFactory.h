#pragma once

#include "event/Event.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace pz::event
{

template <typename EventT, typename DomainT> class EventFactory
{
public:
    EventFactory() = default;
    virtual ~EventFactory() = default;

    virtual std::unique_ptr<EventT> create() = 0;
    virtual std::unique_ptr<EventT> create(std::unique_ptr<pz::ipc::IpcMessage> msg) = 0;
    virtual std::unique_ptr<EventT> create(DomainT domain, std::uint32_t type) = 0;
};

}
