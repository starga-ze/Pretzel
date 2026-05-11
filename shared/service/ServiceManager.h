#pragma once

namespace nf::service
{

class ServiceManager
{
public:
    ServiceManager() = default;
    virtual ~ServiceManager() = default;

    virtual void dispatch() = 0;
};

}
