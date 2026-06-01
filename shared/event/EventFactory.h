#pragma once

#include "event/Event.h"

#include <memory>

namespace nf::ipc
{
    class IpcMessage;
}

namespace nf::event
{

class EventFactory
{
public:
    EventFactory() = default;
    virtual ~EventFactory() = default;

    virtual std::unique_ptr<Event> create() = 0;
    virtual std::unique_ptr<Event> create(std::unique_ptr<nf::ipc::IpcMessage> msg) = 0;
};

} // namespace nf::event
