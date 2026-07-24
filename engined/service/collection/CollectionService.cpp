#include "service/collection/CollectionService.h"

#include "db/Database.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pz::engined
{

using json = nlohmann::json;

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
}

}
