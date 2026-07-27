#pragma once

#include "service/collection/CollectionEvent.h"

#include <string>

namespace pz::engined
{

class EnginedServiceManager;

// Persists API collection samples that scand's scheduler ships over IPC. engined is the sole
// database writer (the same reason keygen results come here via ApiKeyService), so scand collects
// and hands the row over; this only validates the payload and appends it to api_collection.
//
// Pure state, append-only: every poll is one row, never an update — the point is the time series.
class CollectionService
{
public:
    CollectionService() = default;
    ~CollectionService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const CollectionEvent& event);

private:
    void storeSample(const std::string& payloadJson);
};

}
