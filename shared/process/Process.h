#pragma once

namespace nf::process
{

class Process
{
public:
    Process() = default;
    virtual ~Process() = default;

    virtual void tick() = 0;
};

} // namespace nf::process
