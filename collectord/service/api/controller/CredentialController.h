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

    // TLS-only handshake to a device: no credentials sent, just read back the peer certificate's
    // fingerprint so an NGFW can be pinned at device-creation time (before any API Key exists). Routed
    // to on ApiEventType::RunTlsProbe; answers the held ticket with { ok, fingerprint, cert_subject }.
    void runTlsProbe(CollectordServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& input);

    // Seal an API Key's account credential (username/password) and hand it to engined, with no device
    // call and no test outcome. The save path: a password typed in one browser has to be usable from
    // any other, so it goes to the appliance immediately rather than waiting for a passing test.
    // Also refreshes this daemon's credential cache. Routed to on ApiEventType::StoreCredential.
    void storeCredential(ApiService& api, CollectordServiceManager& sm, std::uint32_t seqNo,
                         const nlohmann::json& input);
};

}
