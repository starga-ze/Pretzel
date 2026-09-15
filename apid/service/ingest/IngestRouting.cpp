#include "service/ingest/IngestRouting.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::apid
{

std::string bearerToken(const std::string& authorization)
{
    const std::string prefix = "Bearer ";
    if (authorization.rfind(prefix, 0) != 0)
        return {};
    return authorization.substr(prefix.size());
}

void routeIngest(const pz::http::HttpRequest& req, const std::string& ingestToken, pz::http::HttpResponse& resp)
{
    if (req.method == "GET" && req.target == "/health")
    {
        resp.status = 200;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body = "ok\n";
        return;
    }

    if (req.method == "POST" && req.target == "/api/probe/egress")
    {
        // An empty configured token rejects everything rather than accepting everything: a missing
        // token is a misconfiguration, and the safe reading of it is "nobody is authorised".
        if (ingestToken.empty() || bearerToken(req.authorization) != ingestToken)
        {
            LOG_WARN("egress report rejected (bad or missing bearer token)");
            resp.status = 401;
            resp.body = R"({"error":"unauthorized"})";
            return;
        }

        nlohmann::json obs = nlohmann::json::parse(req.body, nullptr, false);
        if (obs.is_discarded() || !obs.is_object())
        {
            resp.status = 400;
            resp.body = R"({"error":"invalid json"})";
            return;
        }

        const std::string path = obs.value("path", "");
        const std::string tenant = obs.value("tenant", "");
        const std::string deviceId = obs.value("device_id", "");
        const std::string ip = obs.value("ip", "");

        LOG_INFO("egress report accepted (path={}, tenant={}, device_id={}, ip={})", path, tenant, deviceId, ip);

        resp.status = 202;
        resp.body = R"({"status":"accepted"})";
        return;
    }
}

}
