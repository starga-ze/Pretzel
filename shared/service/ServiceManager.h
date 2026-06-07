#pragma once

#include "event/Event.h"
#include "action/Action.h"

#include <memory>

namespace pz::service
{

template <typename EventT, typename ActionT>
class ServiceManager
{
public:
    ServiceManager() = default;
    virtual ~ServiceManager() = default;

    virtual void start() = 0;
    virtual void schedule() = 0;
    virtual void postEvent(std::unique_ptr<EventT> event) = 0;
    virtual void postAction(std::unique_ptr<ActionT> action) = 0;
    virtual void execute() = 0;
};

}
