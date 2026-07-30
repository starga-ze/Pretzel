#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>

namespace pz::collectord
{

class ApiService;
class CollectordServiceManager;

// Issues + persists a vendor credential: a PAN-OS API key (NGFW keygen) or a Prisma Access OAuth
// bearer token (SASE), depending on the target's device type. Owns the keygen-test operation end to
// end — the credential exchange, the persist to engined, and the browser reply — and is the path
// auto-refresh reuses headless (seqNo 0). Routed to directly on ApiEventType::RunKeygenTest.
class CredentialController
{
public:
    void runKeygenTest(ApiService& api, CollectordServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& input);
};

}
