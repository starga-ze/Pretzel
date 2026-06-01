#pragma once

#include "event/Event.h"

#include <memory>

namespace nf::service
{

class ServiceManager
{
public:
    ServiceManager() = default;
    virtual ~ServiceManager() = default;

    virtual void start() = 0;
    virtual void schedule() = 0;
    virtual void dispatch(std::unique_ptr<nf::event::Event> event) = 0;
    virtual void execute() = 0;
};

}
