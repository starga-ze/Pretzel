#pragma once

#include "service/api/controller/ConnectorController.h"
#include "service/api/controller/CredentialController.h"
#include "service/api/controller/NgfwController.h"
#include "service/api/controller/SaseController.h"
#include "service/api/controller/StatusController.h"

#include <boost/asio/io_context.hpp>

#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pz::collectord
{

class ApiEvent;
class ApiAction;
class CollectordEvent;
class CollectordServiceManager;
struct ConnectorTest;

// One reusable set of vendor API credentials, published from the mgmtd UI into
// collectord.service.api.auth_profiles (schema validated in mgmtd's WebService, seeded in
// config/startup-config.json). Secrets are NOT part of the profile: the issued key is sealed
// with the credential store and lives in engined's api_credential_state, keyed by the profile oid.
//
// `oid` is the object's single identity: a UUID string issued at creation, immutable, and what
// inventory objects reference.
struct AuthProfile
{
    std::string oid;
    std::string name;
    std::string vendor;        // "panos"
    std::string username;
    std::string tls;           // "pin" | "strict"
    std::string fingerprint;   // pinned SHA-256, empty until first trusted test
    std::string device;        // device oid this credential is bound to (resolves target/device_type)
    std::string endpoint;      // keygen path (ngfw) / token host+path (sase)
    std::string refreshMode{"manual"};   // "manual" | "auto"
    int refreshIntervalMin{60};          // auto: minutes between re-issues
};

// Which vendor an endpoint speaks to. This is the first branch in every code path that builds a
// call, because nothing below it is shared: an NGFW endpoint is a path on the operator's own box
// authenticated with a PAN-OS key, a SASE endpoint is an absolute URL on Palo Alto's cloud
// authenticated with an OAuth bearer token. Same struct, two disjoint halves.
enum class ApiVendor
{
    Ngfw,
    Sase,
};

// The one sub-choice under a device type. Same slot on both sides, different meaning: for NGFW which
// PAN-OS API the path speaks (it decides how the key is attached), for SASE which cloud product the
// URL belongs to. One field rather than two mutually exclusive ones — they were never both
// meaningful, and having both meant every reader had to know which half of the record it held first.
enum class ApiSubtype
{
    // NGFW.
    Rest,
    Xml,
    // SASE. Only Ztna is served today; the others exist so the seam is named before it is needed.
    Ztna,
    Pab,
    Scm,
};

// One query parameter, stored unencoded exactly as the operator typed it; percent-encoding
// happens when the URL is built.
struct ApiParam
{
    std::string name;
    std::string value;
};

// One complete, callable API request, published into collectord.service.api.endpoints.
//
// Self-contained on purpose: path AND parameters together are what makes a call, which is why
// an endpoint can be tested on its own from the endpoints page without a connector existing.
// Two firewalls needing different arguments — location=vsys&vsys=vsys1 versus vsys2 — are two
// endpoints, not one endpoint parameterised at every use.
//
// The path is versioned by the vendor (/restapi/v10.2/…) and a customer estate runs several
// PAN-OS releases at once, so an upgrade means publishing a SECOND endpoint and re-pointing the
// collections that moved, never editing this one. Both then coexist, and which devices are
// still on the old path is answerable by looking at what references it.
struct ApiEndpoint
{
    std::string oid;
    std::string name;
    std::string path;               // e.g. /restapi/v10.2/Objects/Addresses
    std::vector<ApiParam> params;   // query string — e.g. location=vsys, vsys=vsys1

    ApiVendor vendor{ApiVendor::Ngfw};
    ApiSubtype subtype{ApiSubtype::Rest};

    // SASE only.
    // The cloud host this call goes to. Part of the endpoint rather than the device because a SASE
    // "device" is a tenant, not an address — the tenant is WHO you are (the tsg_id in the token),
    // and the host is WHAT you are calling. One tenant is read through several product hosts.
    std::string host;
    // Operator-editable request headers. The bearer token is NOT one of them — it is minted per call
    // and never stored in configuration. These are the vendor's own routing headers, above all
    // x-panw-region, which cannot be derived and answers 424 rather than an error when wrong.
    std::vector<ApiParam> headers;
};

// One endpoint this connector collects, and how often.
struct ApiCollectionItem
{
    std::string endpointOid;        // resolved against endpoints()
    std::int64_t pollIntervalSec{60};
    bool enabled{true};
};

// The collection policy for one inventory object, published into collectord.service.api.connectors:
// which credential to use against it, and which endpoints to poll at which intervals.
//
// One connector per object — a device is either collected over its API or it is not, and its
// whole schedule lives in one place. Adding a metric to a device is appending an item here, not
// creating another connector.
struct ApiConnector
{
    std::string oid;
    std::string name;
    std::string objectOid;              // inventory object this connector collects from
    std::string authProfileOid;         // credential used against that object
    std::vector<ApiCollectionItem> items;
};

// The durable account credential (ngfw user/pass, sase client id/secret) opened from
// api_credential_state so collectord can re-issue on its own for auto-refresh. Memory-only.
struct IssuedCredential
{
    std::string id;
    std::string pw;
};

class ApiService
{
public:
    // The io_context is needed by the controllers this service owns (the collection timers and the
    // SASE probe's async calls), so it is a construction dependency rather than passed per call.
    explicit ApiService(boost::asio::io_context& ioc);
    ~ApiService() = default;

    void start();

    // Injected each main-loop tick (after bootstrap): returns the next due scheduled event — Setup
    // once, then RunPeriodic — or nullptr. The manager posts it rather than poking the service
    // directly, so all periodic work flows through the event/action queue.
    std::unique_ptr<CollectordEvent> schedule(std::chrono::steady_clock::time_point now);

    void handleEvent(CollectordServiceManager& serviceManager, const ApiEvent& event);
    void handleAction(CollectordServiceManager& serviceManager, const ApiAction& action);

    // Asks engined for the issued keys. Called after config load and whenever one is re-issued;
    // the answer arrives as a separate event, so this returns immediately.
    void requestKeys(CollectordServiceManager& serviceManager);

    // The key issued for an API Key profile, or empty when none is stored yet. Opened from the
    // sealed blob at cache time, so this is plaintext and must not be logged.
    const std::string& issuedKey(const std::string& authProfileOid) const;

    // Caches a key the connector test just had issued, so the operator's next test does not have to
    // re-enter the password before engined's persist round-trip returns.
    void rememberIssuedKey(const std::string& authProfileOid, std::string key);

    // The opened account credential for a profile, or nullptr when none is cached. Used by
    // auto-refresh to re-issue without an operator typing the secret again.
    const IssuedCredential* credentialFor(const std::string& oid) const;

    // Caches a credential the operator just saved, so the next test does not have to wait for
    // engined's persist round-trip to come back. Same lifetime as the rest of the cache: memory only.
    void rememberCredential(const std::string& oid, std::string id, std::string pw);

    const std::vector<AuthProfile>& profiles() const;
    const AuthProfile* findProfile(const std::string& oid) const;

    const std::vector<ApiEndpoint>& endpoints() const;
    const ApiEndpoint* findEndpoint(const std::string& oid) const;

    const std::vector<ApiConnector>& connectors() const;

private:
    void loadProfiles(const nlohmann::json& cfg);
    void loadEndpoints(const nlohmann::json& cfg);
    void loadConnectors(const nlohmann::json& cfg);

    // Applies an ApiCredentialStateResponse: opens each sealed blob and replaces the cache.
    void cacheKeys(const nlohmann::json& payload);

    // Dispatch one event to its handler — a direct switch (no router table, no function pointers),
    // so the flow reads top to bottom. handleEvent delegates here, mirroring WebService::route. Each
    // connector-test case decodes the payload then hands straight to the owning controller.
    void route(CollectordServiceManager& sm, const ApiEvent& event);

    // Decode a connector-test request into its seqNo + JSON payload; false (with a warning) if the
    // message is missing or unparseable. The one shared step before the per-op controller call.
    bool decodeTest(const ApiEvent& event, std::uint32_t& seqNo, nlohmann::json& input) const;

    void handleKeyState(CollectordServiceManager& sm, const ApiEvent& event);
    void handleSetup(CollectordServiceManager& sm, const ApiEvent& event);
    void handleRunPeriodic(CollectordServiceManager& sm, const ApiEvent& event);

    // Re-issue key/token for auto-refresh credentials whose interval has elapsed (RunPeriodic).
    void autoRefreshTick(CollectordServiceManager& sm, std::chrono::steady_clock::time_point now);

    bool m_setupDone{false};
    std::chrono::steady_clock::time_point m_lastPeriodicAt{};
    // authProfileOid -> when it was last re-issued, for the auto-refresh interval.
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_lastRefresh;

    std::vector<AuthProfile> m_profiles;
    // authProfileOid -> issued key, plaintext. Held in memory only: it is re-fetched from
    // engined on every start, so nothing here outlives the process.
    std::unordered_map<std::string, std::string> m_issuedKeys;
    // authProfileOid -> opened account credential (id/pw), for auto-refresh. Same lifetime as m_issuedKeys.
    std::unordered_map<std::string, IssuedCredential> m_credentials;
    // Every controller in service/api/controller/ is a sub-unit of this service, so this service owns
    // all of them — the manager owns services, not their internals. Credential/Endpoint are stateless
    // (each run builds its own async context); Connector holds the collection schedule and Status the
    // SASE probe's state, which is why those two take the io_context.
    CredentialController m_credentialController;
    NgfwController m_ngfwController;
    SaseController m_saseController;
    ConnectorController m_connectorController;
    StatusController m_statusController;
    std::vector<ApiEndpoint> m_endpoints;
    std::vector<ApiConnector> m_connectors;
};

}
