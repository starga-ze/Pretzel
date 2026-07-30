#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>

namespace pz::collectord
{

class ApiService;
class CollectordServiceManager;

// Calls one published endpoint with a key and reports what came back — the endpoint-test operation,
// separated from credential issuance so a wrong path is distinguishable from a wrong credential. If
// the profile has no issued key yet, it issues a transient one first (not persisted — the point here
// is the path, not storing a credential) and then calls. Routed on ApiEventType::RunEndpointTest.
class EndpointController
{
public:
    void runEndpointTest(ApiService& api, CollectordServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& input);
};

}
