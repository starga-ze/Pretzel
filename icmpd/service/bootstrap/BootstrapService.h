#pragma once

#include "event/IcmpdEvent.h"

#include <memory>
#include <chrono>

namespace nf::icmpd
{

class BootstrapService
{
public:
    BootstrapService();
    ~BootstrapService() = default;

    void start();
    std::unique_ptr<nf::event::Event> schedule(std::chrono::steady_clock::time_point now);
    bool isReady();
};

} // namespace nf::icmpd
