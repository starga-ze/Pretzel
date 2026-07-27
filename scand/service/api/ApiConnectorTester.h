#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>

namespace pz::scand
{

class ApiService;
class ScandServiceManager;

// The connector-test use case, lifted out of ApiService the way mgmtd's controllers are lifted out
// of WebService. ApiService stays the config + issued-key repository; this owns the one operation
// that exercises a device — the async keygen/endpoint exchange — reading the cache through
// ApiService::issuedKey and updating it through ApiService::rememberIssuedKey, and nothing else.
//
// There is no router here, unlike mgmtd: scand routes a single IPC event, not many URLs, so the
// seam is one entry point rather than a dispatch table. The stages are free functions private to
// the .cpp; only that entry point is exposed.
class ApiConnectorTester
{
public:
    // Runs the test mgmtd asked for on `seqNo`, answering that held ticket exactly once — including
    // when the request cannot even be started, so a throw never leaves the browser hanging.
    static void run(ApiService& api, ScandServiceManager& sm, std::uint32_t seqNo,
                    const nlohmann::json& input);
};

}
