#include "service/collection/CollectionService.h"

#include "db/Database.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>

namespace pz::engined
{

using json = nlohmann::json;

namespace
{

// How long a sample's call metadata is kept. This is what the Insight ▸ API Collection page trends
// over, and one row is tiny, so it is generous.
constexpr int kRetentionDays = 14;

// How many samples per stream keep their raw response body. Everything older keeps its row but
// releases the payload (body_aged). At 16 KB a body and a 60-second poll, retaining every body for
// the full window would be ~320 MB per stream; retaining the newest 50 is under 1 MB, and 50 covers
// the only question a body is ever opened to answer — "what is this endpoint returning right now".
constexpr int kBodiesPerStream = 50;

constexpr auto kPruneInterval = std::chrono::hours(1);

}

void CollectionService::handleEvent(EnginedServiceManager& serviceManager, const CollectionEvent& event)
{
    (void)serviceManager;

    if (event.type() != CollectionEventType::ReceiveSample)
        return;

    const pz::ipc::IpcMessage* msg = event.message();
    if (!msg || msg->getPayload().empty())
    {
        LOG_WARN("empty ApiCollectionSample — dropping");
        return;
    }

    const auto& body = msg->getPayload();
    storeSample(std::string(reinterpret_cast<const char*>(body.data()), body.size()));
}

void CollectionService::storeSample(const std::string& payloadJson)
{
    json root;
    try
    {
        root = json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ApiCollectionSample payload (error={})", e.what());
        return;
    }

    const std::string connectorOid = root.value("connector_oid", std::string());
    const std::string endpointOid = root.value("endpoint_oid", std::string());
    if (connectorOid.empty() || endpointOid.empty())
    {
        LOG_WARN("ApiCollectionSample without connector/endpoint oid — dropping");
        return;
    }

    // Integers arrive as JSON numbers but travel to libpq as text; an absent one becomes '' and is
    // stored NULL (NULLIF + cast below) rather than a bogus 0.
    auto intField = [&](const char* key) -> std::string {
        return root.contains(key) && root[key].is_number() ? std::to_string(root[key].get<std::int64_t>())
                                                           : std::string();
    };

    const bool ok = root.value("ok", false);
    const bool truncated = root.value("truncated", false);
    const std::string httpStatus = intField("http_status");
    const std::string latencyMs = intField("latency_ms");
    const std::string bytes = intField("bytes");
    const std::string respBody = root.value("body", std::string());
    const std::string error = root.value("error", std::string());

    const bool wrote = pz::db::Database::instance().exec(
        "INSERT INTO api_collection "
        "(connector_oid, endpoint_oid, ok, http_status, latency_ms, bytes, truncated, body, error) "
        "VALUES ($1, $2, $3::boolean, NULLIF($4,'')::int, NULLIF($5,'')::int, NULLIF($6,'')::int, "
        "$7::boolean, NULLIF($8,''), NULLIF($9,''))",
        {connectorOid, endpointOid, ok ? "true" : "false", httpStatus, latencyMs, bytes, truncated ? "true" : "false",
         respBody, error});

    if (wrote)
        LOG_INFO("collection sample stored (connector={}, endpoint={}, ok={}, status={}, bytes={})", connectorOid,
                 endpointOid, ok, httpStatus.empty() ? "-" : httpStatus, bytes.empty() ? "-" : bytes);
    else
        LOG_WARN("api_collection write failed (connector={}, endpoint={})", connectorOid, endpointOid);

    pruneIfDue();
}

void CollectionService::pruneIfDue()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastPrune < kPruneInterval)
        return;

    m_lastPrune = now;   // set first: a failing sweep must not retry on every subsequent sample
    prune();
}

void CollectionService::prune()
{
    auto& db = pz::db::Database::instance();

    // Whole rows past the metadata window.
    db.exec("DELETE FROM api_collection WHERE collected_at < now() - ($1 || ' days')::interval",
            {std::to_string(kRetentionDays)});

    // Bodies past the body window: the row (and so the trend) survives, the payload is released.
    // `body_aged` is what lets the UI say "released" instead of implying the poll returned nothing,
    // and it doubles as the idempotence guard — an already-swept row cannot match again.
    db.exec("UPDATE api_collection SET body = NULL, body_aged = true WHERE oid IN ("
            "  SELECT oid FROM ("
            "    SELECT oid, row_number() OVER (PARTITION BY connector_oid, endpoint_oid "
            "                                   ORDER BY collected_at DESC) AS rn"
            "    FROM api_collection WHERE body IS NOT NULL"
            "  ) ranked WHERE rn > $1::int)",
            {std::to_string(kBodiesPerStream)});

    LOG_DEBUG("api_collection pruned (retention={}d, bodies_per_stream={})", kRetentionDays, kBodiesPerStream);
}

}
