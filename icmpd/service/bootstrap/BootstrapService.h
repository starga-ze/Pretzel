#pragma once

#include "service/bootstrap/BootstrapEvent.h"

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

    void handleEvent(const IcmpdEvent& event);

private:
    enum class State
    {
        Init, 
        WaitServerHello,
        Ready,
        Failed
    };

    State m_state {State::Init};

    std::chrono::steady_clock::time_point m_startedAt {};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt {};
};

} // namespace nf::icmpd
