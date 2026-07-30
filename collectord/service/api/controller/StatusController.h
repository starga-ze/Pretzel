#pragma once

#include <boost/asio/io_context.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pz::collectord
{

class ApiService;
class CollectordServiceManager;
struct ConnectorTest;

// Periodic SASE (Prisma Access) health probe. A SASE tenant has no pingable IP, so reachability is a
// control-plane API read rather than ICMP: getPrismaAccessIP with the device's own api-key. A 200 +
// {"status":"success"} means the tenant's SASE control plane and the api-key are healthy, so the
// device is "active" — the SASE equivalent of an ICMP echo reply. Runs asynchronously on the shared
// io_context, so the slow (seconds-long) HTTPS call never blocks the probe loop; results are
// eventually-consistent and piggyback on the ICMP ProbeResult probed already sends to engined.
//
// The api-key is NOT in running_config — it lives sealed in sase_device.api_key_enc and reaches
// probed through the same ApiCredentialState fetch as issued keys, so the probe looks it up by
// device oid in ApiService's cache. A device with no cached key (not configured, or the fetch has
// not landed) is skipped, so engined leaves its status untouched (unknown).
class StatusController
{
public:
    explicit StatusController(boost::asio::io_context& ioc);

    // Fire the due probes (one per configured SASE device) when the interval has elapsed, and ship
    // the previous cycle's outcome to engined as a SaseHealthResult (rate-limited by the same
    // interval gate). `api` supplies the sealed-and-opened api-key by device oid. Returns immediately.
    void tick(std::chrono::steady_clock::time_point now, const ApiService& api, CollectordServiceManager& sm);

    // On-demand SASE device health test (the operator's "test"): run getPrismaAccessIP with the
    // entered api-key and, on success, seal+persist it to sase_device.api_key_enc — the same call as
    // the periodic probe, but validating and storing a key the operator just typed. Answers the held
    // ticket. Routed to directly on ApiEventType::RunSaseTest.
    void runSaseTest(CollectordServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& input);

    std::vector<std::string> aliveTargets() const;
    std::vector<std::string> downTargets() const;

    // target -> last successful getPrismaAccessIP response, for engined to cache in
    // sase_device.egress_result. Only successful probes leave an entry.
    const std::unordered_map<std::string, nlohmann::json>& egressResults() const;

private:
    boost::asio::io_context& m_ioc;
    std::chrono::steady_clock::time_point m_lastRun{};
    std::unordered_map<std::string, bool> m_result;               // target -> last probe ok
    std::unordered_map<std::string, bool> m_inFlight;              // target -> a probe is running
    std::unordered_map<std::string, nlohmann::json> m_egress;      // target -> last egress result
};

}
