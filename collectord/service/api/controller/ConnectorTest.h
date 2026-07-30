#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace pz::collectord
{

class ApiService;
class CollectordServiceManager;

// One device the operator asked to test. Built from the IPC payload rather than from the loaded
// profiles: the operator runs a test on values typed into the UI that have not been committed yet,
// so the request has to carry the whole target.
struct TestTarget
{
    std::string host;
    std::uint16_t port{443};
    std::string fingerprint;   // pinned SHA-256, empty on first contact
    std::string username;      // ngfw: admin user   · sase: OAuth client id
    std::string password;      // ngfw: admin passwd · sase: OAuth client secret
    std::string keygenEndpoint;
    std::string authProfileOid;   // which API Key profile — used to find an already-issued key
    std::string deviceType{"ngfw"};   // ngfw | sase — selects the key-issuance flow
};

// One in-flight connector test, threaded through the async stages as a shared_ptr so the state
// outlives each completion callback — the daemon loop keeps running while a slow or unreachable
// device is waited on. `api`/`sm` are back-pointers to objects that live for the process; the stages
// live in the per-domain controllers (Credential/Endpoint/Status) and hand off through
// ApiService::afterKey, so a keyless endpoint test can issue a key first and then call. `out` is the
// result document mgmtd relays to the browser.
struct ConnectorTest
{
    ApiService* api{nullptr};
    CollectordServiceManager* sm{nullptr};
    std::uint32_t seqNo{0};
    TestTarget target;
    std::string mode;      // "keygen" | "endpoint" | "sase_health"
    std::string keyOid;    // API Key record oid the issued key is persisted under (keygen mode)
    std::string expiresAt; // issued-key expiry, ISO-8601 UTC — set for SASE tokens, empty for NGFW
    nlohmann::json input;  // original request payload — endpoint path/params are read back from here
    nlohmann::json out;    // the result document
};

}
