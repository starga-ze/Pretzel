#pragma once

#include "event/Event.h"

#include <memory>

namespace nf::service
{

template <typename EventT>
class ServiceManager
{
public:
    ServiceManager() = default;
    virtual ~ServiceManager() = default;

    virtual void start() = 0;
    virtual void schedule() = 0;
    virtual void postEvent(std::unique_ptr<EventT> event) = 0;
    virtual void execute() = 0;
};

}
