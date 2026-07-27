#pragma once

#include <memory>
#include <vector>

namespace pz::scand
{

class ApiService;
class ScandServiceManager;

// One in-flight schedule, defined in ApiCollector.cpp. Held by shared_ptr so it outlives each
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
class ApiCollector
{
public:
    ApiCollector();
    ~ApiCollector();

    void start(ScandServiceManager& sm, ApiService& api);

private:
    std::vector<std::shared_ptr<CollectorJob>> m_jobs;
};

}
