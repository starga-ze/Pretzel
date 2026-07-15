#pragma once

#include "service/probe/ProbeEvent.h"

#include <chrono>
#include <memory>

namespace pz::engined
{

class EnginedServiceManager;

class ProbeService
{
public:
    ProbeService() = default;
    ~ProbeService() = default;

    void start();

    std::unique_ptr<EnginedEvent> schedule(std::chrono::steady_clock::time_point now);

    void handleEvent(EnginedServiceManager& serviceManager, const ProbeEvent& event);

private:
    void sendProbeRequest(EnginedServiceManager& serviceManager);
    void onProbeResult(EnginedServiceManager& serviceManager, const ProbeEvent& event);

    bool m_pending{false};
    std::chrono::steady_clock::time_point m_lastPollAt{};
    std::chrono::steady_clock::time_point m_requestedAt{};
};

}
