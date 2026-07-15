#pragma once

namespace pz::process
{

class Process
{
public:
    Process() = default;
    virtual ~Process() = default;

    virtual bool start() = 0;
    virtual void tick() = 0;
};

}
