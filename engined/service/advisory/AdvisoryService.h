#pragma once

#include "service/advisory/AdvisoryEvent.h"

#include <string>

namespace pz::engined
{

class EnginedServiceManager;

// Persists the security advisories collectord collects from the vendor's published feed. engined is
// the sole database writer (the same reason collection samples and keygen results come here), so
// collectord makes the outbound call and hands the rows over; this validates them and writes.
//
// Upsert, not append. api_collection keeps one row per poll because the point there is the time
// series of the CALL; here the point is the advisory, and an advisory is one document that the
// vendor keeps editing — a quarter of them are revised after publication. A hundred polls of an
// unchanged advisory must leave one row untouched, not a hundred rows that nearly agree.
//
// What makes a revision is the content hash collectord computes over the fields that mean
// something, NOT the feed's own `updated` timestamp — that field is also the RSS sort key and moves
// for reasons that are not content. On a genuine change the row's revised_at is stamped and its
// notified_at is cleared, which is what later hands the row to the alert sweep a second time.
class AdvisoryService
{
public:
    AdvisoryService() = default;
    ~AdvisoryService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const AdvisoryEvent& event);

private:
    void onUpdate(const AdvisoryEvent& event);
    void storeBatch(const std::string& payloadJson);
};

}
