#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace pz::scand
{

class ApiEvent;
class ScandServiceManager;

// One reusable set of vendor API credentials, published from the mgmtd UI into
// scand.service.api.auth_profiles (schema validated in mgmtd's WebService, seeded in
// config/startup-config.json). Secrets are NOT part of the profile: the issued key is sealed
// with the credential store and lives in engined's api_key_state, keyed by the profile oid.
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
};

// PAN-OS serves two APIs behind one credential. Which one a call speaks is a property of the
// path, not of the device, so it travels with the endpoint.
enum class ApiType
{
    Xml,
    Rest,
};

// One query parameter, stored unencoded exactly as the operator typed it; percent-encoding
// happens when the URL is built.
struct ApiParam
{
    std::string name;
    std::string value;
};

// One complete, callable API request, published into scand.service.api.endpoints.
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
    ApiType apiType{ApiType::Rest};
    std::vector<ApiParam> params;   // e.g. location=vsys, vsys=vsys1
};

// One endpoint this connector collects, and how often.
struct ApiCollectionItem
{
    std::string endpointOid;        // resolved against endpoints()
    std::int64_t pollIntervalSec{60};
    bool enabled{true};
};

// The collection policy for one inventory object, published into scand.service.api.connectors:
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

// One device the operator asked to test. Built from the IPC payload rather than from the
// loaded connectors: the operator runs a test on values typed into the UI that have not been
// committed yet, so the request has to carry the whole target.
struct TestTarget
{
    std::string host;
    std::uint16_t port{443};
    std::string fingerprint;   // pinned SHA-256, empty on first contact
    std::string username;
    std::string password;
    std::string keygenEndpoint;
    std::string authProfileOid;   // which API Key profile — used to find an already-issued key
};

class ApiService
{
public:
    ApiService() = default;
    ~ApiService() = default;

    void start();

    void handleEvent(ScandServiceManager& serviceManager, const ApiEvent& event);

    // Asks engined for the issued keys. Called after config load and whenever one is re-issued;
    // the answer arrives as a separate event, so this returns immediately.
    void requestKeys(ScandServiceManager& serviceManager);

    // The key issued for an API Key profile, or empty when none is stored yet. Opened from the
    // sealed blob at cache time, so this is plaintext and must not be logged.
    const std::string& issuedKey(const std::string& authProfileOid) const;

    // Caches a key the connector test just had issued, so the operator's next test does not have to
    // re-enter the password before engined's persist round-trip returns. The one write the
    // ApiConnectorTester makes back into this service; the read side is issuedKey().
    void rememberIssuedKey(const std::string& authProfileOid, std::string key);

    const std::vector<AuthProfile>& profiles() const;
    const AuthProfile* findProfile(const std::string& oid) const;

    const std::vector<ApiEndpoint>& endpoints() const;
    const ApiEndpoint* findEndpoint(const std::string& oid) const;

    const std::vector<ApiConnector>& connectors() const;

private:
    void loadProfiles(const nlohmann::json& cfg);
    void loadEndpoints(const nlohmann::json& cfg);
    void loadConnectors(const nlohmann::json& cfg);

    // Applies an ApiKeyStateResponse: opens each sealed blob and replaces the cache.
    void cacheKeys(const nlohmann::json& payload);

    std::vector<AuthProfile> m_profiles;
    // authProfileOid -> issued key, plaintext. Held in memory only: it is re-fetched from
    // engined on every start, so nothing here outlives the process.
    std::unordered_map<std::string, std::string> m_issuedKeys;
    std::vector<ApiEndpoint> m_endpoints;
    std::vector<ApiConnector> m_connectors;
};

}
