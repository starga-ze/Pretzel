#include "service/api/CollectionSample.h"

#include "http/HttpClient.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace pz::scand
{

using json = nlohmann::json;

json buildCollectionSample(const std::string& connectorOid, const std::string& endpointOid,
                           const pz::http::ClientResponse& res, long long latencyMs, std::size_t maxBody)
{
    const bool ok = res.tlsOk && res.requestSent && res.status == 200;

    json sample;
    sample["connector_oid"] = connectorOid;
    sample["endpoint_oid"] = endpointOid;
    sample["ok"] = ok;
    sample["latency_ms"] = latencyMs;
    sample["bytes"] = static_cast<std::int64_t>(res.body.size());

    // Only meaningful when the request left the machine; a pin gate refusal never got a status.
    if (res.requestSent)
        sample["http_status"] = res.status;

    const bool truncated = res.body.size() > maxBody;
    sample["truncated"] = truncated;
    sample["body"] = truncated ? res.body.substr(0, maxBody) : res.body;

    if (!ok)
    {
        std::string error;
        if (!res.tlsOk)
            error = res.error.empty() ? "TLS handshake failed" : res.error;
        else if (!res.requestSent)
            error = res.error.empty() ? "certificate not trusted — pin the device first" : res.error;
        else
            error = "HTTP " + std::to_string(res.status);
        sample["error"] = error;
    }

    return sample;
}

}
