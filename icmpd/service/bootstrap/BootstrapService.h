#pragma once

#include "service/bootstrap/BootstrapEvent.h"
#include "service/bootstrap/BootstrapAction.h"

#include "event/IcmpdEventFactory.h"
#include "action/IcmpdActionFactory.h"

#include <memory>
#include <chrono>

namespace nf::icmpd
{

class BootstrapService
{
public:
    BootstrapService(IcmpdEventFactory* eventFactory, IcmpdActionFactory* actionFactory);
    ~BootstrapService() = default;

    void start();
    std::unique_ptr<IcmpdEvent> schedule(std::chrono::steady_clock::time_point now);
    bool isReady();

    void handleEvent(const BootstrapEvent& event);

private:
    IcmpdEventFactory* m_eventFactory;
    IcmpdActionFactory* m_actionFactory;

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
