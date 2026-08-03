#pragma once

#include "service/api/controller/ConnectorTest.h"

#include "http/HttpClient.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace pz::collectord
{

class CollectordServiceManager;

// Shared, side-effect-light helpers for the Api service — the plumbing the connector-test controllers
// (Credential / Endpoint / Status) and the periodic collector all repeat, kept free so each owns only
// its own device exchange. (Consolidates the former TestSupport + CollectionSample units.)

// ── Connector-test helpers ────────────────────────────────────────────────────────────────

nlohmann::json stepJson(bool ok, std::string detail);

// Build a TestTarget from the operator's (possibly uncommitted) request payload.
TestTarget parseTestTarget(const nlohmann::json& body);

// A pinned HTTPS request to the device under test (timeout + expected fingerprint filled in).
pz::http::ClientRequest baseRequest(const TestTarget& t);

// Populate the `tls` step and say whether the caller may continue. First contact or pin mismatch
// stops here — HttpClient refuses to transmit credentials to an unverified peer.
bool recordTlsStep(const pz::http::ClientResponse& r, bool hadPin, nlohmann::json& out);

std::string extractXmlTag(const std::string& xml, const std::string& tag);
std::string xmlErrorMessage(const std::string& xml);

// Build the PAN-OS keygen request: the pinned base request with the account credentials appended as
// query parameters (the form every release accepts). Shared so CredentialController (which persists
// the key) and EndpointController (which uses it transiently) issue keys the exact same way.
pz::http::ClientRequest buildKeygenRequest(const TestTarget& t);

// Read the issued key back from a keygen response: records the tls + auth steps into `out` and
// returns the key, or an empty string on any failure (with out["message"]/steps set to say why).
std::string readKeygenKey(const pz::http::ClientResponse& res, bool hadPin, nlohmann::json& out);

// Answer mgmtd's held ticket. seqNo 0 is the headless auto-refresh path — nothing is waiting.
void sendTestResponse(CollectordServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& out);

// Hand the issued key + account credential to engined (the only DB writer), each sealed here so
// plaintext never crosses the socket, and each written only when present (engined COALESCEs).
void sendApiCredentialState(CollectordServiceManager& sm, const std::string& keyOid, const std::string& id,
                            const std::string& pw, const std::string& key, const std::string& expiresAt, bool ok,
                            const std::string& note);

// Seal and store just the account credential, with no test outcome attached — the payload carries no
// `ok`, so engined leaves the last_test_* columns alone. This is the save path: the operator's
// password belongs on the appliance, not in whichever browser happened to type it. Returns false when
// the credential store is unavailable (nothing was sent).
bool sendApiCredentialStore(CollectordServiceManager& sm, const std::string& keyOid, const std::string& id,
                            const std::string& pw);

// Reject a test up front (validation) — a single response on the held ticket.
void rejectTest(const std::shared_ptr<ConnectorTest>& ctx, const std::string& message);

// ── Collection sample ─────────────────────────────────────────────────────────────────────

// Builds the api_collection sample document from one device call's outcome — the shape collectord
// ships to engined. A pure function of its inputs (no timer, no IPC, no config), which is what makes
// it unit-testable in isolation.
//
//   ok          : tlsOk && requestSent && HTTP 200 — a usable response, not merely a reply
//   http_status : present only when the request was actually sent (a pin gate stops before that)
//   body        : the response, cut to `maxBody`; `truncated` says whether it was
//   error       : set only when !ok, naming the stage that failed
nlohmann::json buildCollectionSample(const std::string& connectorOid, const std::string& endpointOid,
                                     const pz::http::ClientResponse& res, long long latencyMs,
                                     std::size_t maxBody);

}
