#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace pz::http
{
struct ClientResponse;
}

namespace pz::collectord
{

struct ApiEndpoint;

class ApiService;
class CollectordServiceManager;

// One in-flight schedule, defined in ConnectorController.cpp. Held by shared_ptr so it outlives each
// timer wait and async device call.
struct CollectorJob;

// Periodic API collection: for every enabled item of every connector, poll its endpoint on the
// item's interval and ship the result to engined (the sole DB writer) for api_collection. Reads
// the schedule, endpoints and issued keys from ApiService, and resolves each device's host +
// pinned fingerprint from config; the device exchange reuses the same pinned HTTPS client as the
// connector test.
//
// Not the tester and not a router: the seam is one start() that arms a repeating steady_timer per
// item. A config reload restarts the daemon, so there is no live re-arm — start() runs once against
// the freshly loaded config.
class ConnectorController
{
public:
    ConnectorController();
    ~ConnectorController();

    void start(CollectordServiceManager& sm, ApiService& api);

    // Bring one item's next poll forward to now. Both oids must match, because a connector may
    // collect several endpoints and only the one the operator is looking at should be disturbed.
    //
    // Re-arms the existing timer rather than starting a second poll: the job owns the in-flight
    // guard, and two overlapping reads of the same endpoint would land as two samples a moment
    // apart with no way to tell which is newer.
    void runNow(const std::string& connectorOid, const std::string& endpointOid);

private:
    // One job's cycle: arm the timer, poll when it fires, and re-arm from the response. Declared
    // here and defined in the .cpp like any other member — as members the four call one another
    // whatever order they are defined in, so the cycle needs no forward declarations.
    //
    // Each still carries its job: unlike the advisory collector, which walks one feed, this runs
    // one job per connector item and the shared_ptr is what tells a completion which it belongs to
    // — and what keeps that job alive across the wait and the call.
    void armJob(std::shared_ptr<CollectorJob> job, std::chrono::seconds delay);
    void collectOnce(std::shared_ptr<CollectorJob> job);
    void collectSaseOnce(std::shared_ptr<CollectorJob> job, const ApiEndpoint& ep, const std::string& token);
    void onResponse(std::shared_ptr<CollectorJob> job, std::chrono::steady_clock::time_point startedAt,
                    const pz::http::ClientResponse& res);

    std::vector<std::shared_ptr<CollectorJob>> m_jobs;
};

}
