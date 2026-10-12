#pragma once

#include "service/probe/ProbeEvent.h"

#include <chrono>
#include <memory>

namespace pz::engined
{

class EnginedServiceManager;
class EnginedEventFactory;

class ProbeService
{
public:
    // See HeartbeatService: the probe-due event this raises for itself is made through the
    // factory, not with make_unique, so every event in the daemon is born in one place.
    explicit ProbeService(EnginedEventFactory* eventFactory);
    ~ProbeService() = default;

    void start();

    std::unique_ptr<EnginedEvent> schedule(std::chrono::steady_clock::time_point now);

    void handleEvent(EnginedServiceManager& serviceManager, const ProbeEvent& event);

private:
    EnginedEventFactory* m_eventFactory{nullptr};

    void sendProbeRequest(EnginedServiceManager& serviceManager);
    void onProbeResult(EnginedServiceManager& serviceManager, const ProbeEvent& event);

    // SASE control-plane health from collectord (SaseHealthResult): reflect alive/down + egress into
    // sase_device. Separate message from ProbeResult because the SASE probe runs in collectord.
    void onSaseHealthResult(EnginedServiceManager& serviceManager, const ProbeEvent& event);

    // Project the operator-declared objects (probed.probe.probe_targets) into the inventory
    // table (config is the source of truth); called each probe cycle before status is applied.
    void projectInventory();

    bool m_pending{false};
    std::chrono::steady_clock::time_point m_lastPollAt{};
    std::chrono::steady_clock::time_point m_requestedAt{};
};

}
