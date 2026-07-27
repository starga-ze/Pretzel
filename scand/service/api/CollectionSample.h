#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <string>

namespace pz::http
{
struct ClientResponse;
}

namespace pz::scand
{

// Builds the api_collection sample document from one device call's outcome — the shape scand ships
// to engined. Split out of ApiCollector so it is a pure function of its inputs (no timer, no IPC,
// no config), which is what makes it unit-testable in isolation.
//
//   ok        : tlsOk && requestSent && HTTP 200 — a usable response, not merely a reply
//   http_status : present only when the request was actually sent (a pin gate stops before that)
//   body      : the response, cut to `maxBody`; `truncated` says whether it was
//   error     : set only when !ok, naming the stage that failed
nlohmann::json buildCollectionSample(const std::string& connectorOid, const std::string& endpointOid,
                                     const pz::http::ClientResponse& res, long long latencyMs,
                                     std::size_t maxBody);

}
