#pragma once

#include "service/probe/ProbeEvent.h"
#include "service/probe/ProbeAction.h"

#include <chrono>
#include <memory>

namespace nf::icmpd
{
class IcmpdServiceManager;
class IcmpdEventFactory;
class IcmpdActionFactory;
class IcmpPacket;

class ProbeService
{

public:

    enum class State
    {
        Init,
        NotProbing,
        IsProbing
    };

    ProbeService(IcmpdEventFactory* eventFactory, IcmpdActionFactory* actionFactory);
    ~ProbeService() = default;

    void start();

    std::unique_ptr<IcmpdEvent> schedule(std::chrono::steady_clock::time_point now);
    void handleEvent(IcmpdServiceManager& serviceManager, const ProbeEvent& event);
    void handleAction(IcmpdServiceManager& serviceManager, const ProbeAction& action);

private:
    std::unique_ptr<IcmpPacket> buildEchoRequestPacket() const;

    IcmpdEventFactory* m_eventFactory;
    IcmpdActionFactory* m_actionFactory;

    State m_state{State::Init};
};

} // namespace nf::icmpd
