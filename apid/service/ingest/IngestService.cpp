#include "service/ingest/IngestService.h"

#include "service/ingest/HttpEvent.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <cstdlib>

namespace pz::apid
{

namespace
{

std::string ingestTokenFromEnv()
{
    const char* v = std::getenv("PZ_APID_TOKEN");
    return (v && *v) ? std::string(v) : std::string("changeme-dev-token");
}

} // namespace

IngestService::IngestService()
    : m_ingestToken(ingestTokenFromEnv())
{
}

void IngestService::handleEvent(ApidServiceManager& serviceManager, const HttpEvent& event)
{
    (void)serviceManager;  // used once EgressReport forwarding is wired to the TxRouter

    const auto& req  = event.request();
    auto&       resp = event.response();

    // Public liveness probe.
    if (req.method == "GET" && req.target == "/health")
    {
        resp.status      = 200;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body        = "ok\n";
        return;
    }

    // Token-guarded ingest.
    if (req.method == "POST" && req.target == "/api/probe/egress")
    {
        // Fail-closed auth.
        if (m_ingestToken.empty() || bearerToken(req.authorization) != m_ingestToken)
        {
            LOG_WARN("egress report rejected (bad or missing bearer token)");
            resp.status = 401;
            resp.body   = R"({"error":"unauthorized"})";
            return;
        }

        nlohmann::json obs = nlohmann::json::parse(req.body, nullptr, false);
        if (obs.is_discarded() || !obs.is_object())
        {
            resp.status = 400;
            resp.body   = R"({"error":"invalid json"})";
            return;
        }

        const std::string path     = obs.value("path", "");   // "EP" (browser) | "GP" (tunnel)
        const std::string tenant   = obs.value("tenant", "");
        const std::string deviceId = obs.value("device_id", "");
        const std::string ip       = obs.value("ip", "");

        // TODO(next step): serviceManager.postAction(EgressReport IPC action) → TxRouter →
        // engined (single DB writer). For now the edge accepts + logs.
        LOG_INFO("egress report accepted (path={}, tenant={}, device_id={}, ip={})",
                 path, tenant, deviceId, ip);

        resp.status = 202;
        resp.body   = R"({"status":"accepted"})";
        return;
    }

    // Unknown route: leave the default 404 the handler seeded.
}

std::string IngestService::bearerToken(const std::string& authorization) const
{
    const std::string prefix = "Bearer ";
    if (authorization.rfind(prefix, 0) != 0)
        return {};
    return authorization.substr(prefix.size());
}

} // namespace pz::apid
