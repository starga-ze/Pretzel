#include "service/ingest/IngestService.h"

#include "service/ingest/IngestEvent.h"
#include "service/ingest/IngestAction.h"
#include "service/ApidServiceManager.h"

#include "router/ApidTxRouter.h"

#include "util/Logger.h"

#include <memory>

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

void IngestService::handleEvent(ApidServiceManager& serviceManager, const IngestEvent& event)
{
    pz::http::HttpResponse resp;  // default 404 for unmatched routes
    route(event.request(), resp);

    // Event -> Action: post an IngestAction carrying the response + the SessionId. Egress
    // happens later, in handleAction (the action drain), never inline here.
    serviceManager.postAction(
        std::make_unique<IngestAction>(std::move(resp), event.sessionId()));
}

void IngestService::handleAction(ApidServiceManager& serviceManager, IngestAction& action)
{
    // Egress via the TxRouter — the analogue of BootstrapService::handleAction sending an IPC
    // message. The response body is moved (not copied).
    serviceManager.txRouter().handleHttpMessage(std::move(action.response()), action.sessionId());
}

void IngestService::route(const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
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
