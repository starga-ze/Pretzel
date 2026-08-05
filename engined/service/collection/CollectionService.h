#pragma once

#include "service/collection/CollectionEvent.h"

#include <chrono>
#include <string>

namespace pz::engined
{

class EnginedServiceManager;

// Persists API collection samples that collectord's scheduler ships over IPC. engined is the sole
// database writer (the same reason keygen results come here via ApiCredentialService), so collectord collects
// and hands the row over; this only validates the payload and appends it to api_collection.
//
// Pure state, append-only: every poll is one row, never an update — the point is the time series.
//
// Append-only needs a floor, and this table has two very different tenants in one row: a few dozen
// bytes of call metadata (when, ok, status, latency) and up to 64 KB of raw response body. They are
// worth keeping for different lengths of time — the history that draws a trend is cheap and wanted
// for weeks, the payload is expensive and only ever read for a recent sample. prune() therefore ages
// them separately rather than picking one compromise retention for both.
class CollectionService
{
public:
    CollectionService() = default;
    ~CollectionService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const CollectionEvent& event);

private:
    void storeSample(const std::string& payloadJson);

    // Retention, run from the sample path rather than a timer of its own: samples arriving IS the
    // signal that the table is growing, and a daemon collecting nothing needs no sweep. Rate-limited
    // internally to kPruneInterval, so a 5-second connector does not sweep every 5 seconds.
    void pruneIfDue();
    void prune();

    // Never pruned before the first interval has elapsed since start; a fresh process should not
    // spend its first sample's latency on a table-wide DELETE.
    std::chrono::steady_clock::time_point m_lastPrune{std::chrono::steady_clock::now()};
};

}
