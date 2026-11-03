#pragma once

#include "service/advisory/AdvisoryEvent.h"

#include <chrono>
#include <memory>
#include <string>

namespace pz::http
{
struct ClientResponse;
}

namespace pz::collectord
{

class CollectordServiceManager;
class CollectordEventFactory;

// The state one walk of the feed carries — where it is, what it has collected, and the timer that
// paces it. Defined in AdvisoryService.cpp; the service owns exactly one and reuses it.
struct AdvisoryRun;

// Security advisories: the vendor's published vulnerability feed, collected on a schedule and
// handed to engined.
//
// Its own service rather than a controller under ApiService, which is where it started. ApiService
// is not "the daemon's outbound HTTP" — it is the vendor CREDENTIAL domain: auth profiles, issued
// keys, endpoints, connectors, and the collection those drive. The advisory feed shares none of
// that. It is public, so it touches no credential; it belongs to no device, so it has no connector
// or endpoint. engined already files advisories under a service of their own; this is the same
// seam on the other side of the wire.
//
// No controller layer under it, either. That layer exists in exactly two places — ApiService, which
// is six controllers and twenty-one files, and mgmtd's WebService — and it is what a service builds
// when it has grown too large to read in one file. This one is a timer, a fetch chain and a parse;
// it belongs beside HeartbeatService and ReloadService, which keep their logic in the service and
// are the better model for it.
//
// Shaped like ApiService's connector collector all the same: one repeating steady_timer, every
// stage asynchronous, nothing blocking. A run walks the feed a page at a time and ships what it
// parsed to engined in batches sized by their serialised length — the whole feed is several
// megabytes and the IPC frame is 1 MiB, so the batch boundary is a transport constraint.
//
// Politeness is part of the design, not an afterthought. The feed is someone else's public
// service: pages are spaced by a throttle, the run happens every few hours rather than every
// minute, and the request carries a User-Agent naming this product so the far end can tell who is
// calling.
class AdvisoryService
{
public:
    explicit AdvisoryService(CollectordEventFactory* eventFactory);
    ~AdvisoryService();

    // Injected each main-loop tick (after bootstrap): the one-shot Setup, then nothing. Everything
    // after arming is driven by the run's own timer, so unlike ApiService there is no recurring
    // event — the queue is not this service's clock.
    std::unique_ptr<CollectordEvent> schedule(std::chrono::steady_clock::time_point now);

    void handleEvent(CollectordServiceManager& serviceManager, const AdvisoryEvent& event);

private:
    // Arms the first run shortly after start, then every interval. Takes the io_context from the
    // manager, the way ApiService's connector jobs take it. Reads its settings from the
    // `pretzel.advisory` config section, falling back to defaults when the section is absent —
    // which it is on every appliance that has not been reconfigured since this shipped.
    void start(CollectordServiceManager& sm);

    // Collect now, ahead of the interval. A run already in flight is left alone: the operator is
    // asking for current data and some is on its way.
    void runNow();

    // One walk of the feed, as a cycle: arm the timer, fetch a page, read it, and either arm the
    // next page or finish and arm the next run. Declared here and defined in the .cpp like any
    // other member — the cycle needs no forward declarations because a member may call any other
    // member whatever order they are defined in.
    void armRun(std::chrono::milliseconds delay);
    void fetchPage();
    void onPage(const pz::http::ClientResponse& res);
    void finishRun(const std::string& status, const std::string& error);
    void sendBatch(bool final, const std::string& status, const std::string& error);

    CollectordEventFactory* m_eventFactory{nullptr};
    bool m_setupDone{false};
    // Created by start() and owned outright — there is one run object for the life of the daemon,
    // reused by every collection. Incomplete here on purpose: holding it by pointer keeps
    // Boost.Asio and the feed's internals out of this header, which is why ~AdvisoryService is
    // declared above and defined in the .cpp.
    std::unique_ptr<AdvisoryRun> m_run;
};

}
