#pragma once

#include "service/probe/ProbeEvent.h"

#include <chrono>
#include <memory>

namespace pz::engined
{

class EnginedServiceManager;

// engined-owned ICMP probe orchestrator. On its own timer it asks icmpd to run one
// probe cycle (ProbeRequest), then stores the returned alive-IP snapshot in the
// service manager so the SNMP scan orchestrator can consume it. Mirrors the
// HeartbeatService timer/round pattern.
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

} // namespace pz::engined
