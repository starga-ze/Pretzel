#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
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

// One collection binding, published into scand.service.api.connectors: the inventory object it
// targets (which carries the auth profile reference) plus how often to poll it. scand runs one
// collection loop per enabled connector.
// PAN-OS serves two APIs behind one credential; the operator states which one this connector
// speaks and the exact path to call, because customer devices run versions we do not control
// (the REST path carries the version, the XML path does not).
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

struct ApiConnector
{
    std::string oid;
    std::string name;
    std::string objectOid;          // inventory object this connector collects from
    std::string authProfileOid;     // credential used against that object
    ApiType apiType{ApiType::Rest};
    std::string endpoint;           // operator-entered path, e.g. /restapi/v10.2/Objects/Addresses
    std::vector<ApiParam> params;   // e.g. location=vsys, vsys=vsys1
    std::int64_t pollIntervalSec{60};
    bool enabled{true};
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
};

class ApiService
{
public:
    ApiService() = default;
    ~ApiService() = default;

    void start();

    void handleEvent(ScandServiceManager& serviceManager, const ApiEvent& event);

    const std::vector<AuthProfile>& profiles() const;
    const AuthProfile* findProfile(const std::string& oid) const;

    const std::vector<ApiConnector>& connectors() const;

private:
    void loadProfiles(const nlohmann::json& cfg);
    void loadConnectors(const nlohmann::json& cfg);

    // Both tests begin by exchanging username/password for an API key; the endpoint test then
    // continues with that key. Each stage is an async completion, so the daemon loop keeps
    // running while a slow or unreachable device is waited on.
    void runConnectorTest(ScandServiceManager& serviceManager, std::uint32_t seqNo, const nlohmann::json& input);

    void runKeygen(ScandServiceManager& serviceManager, const TestTarget& target,
                   std::function<void(const std::string& key, nlohmann::json& out)> onDone,
                   std::shared_ptr<nlohmann::json> out);

    void runEndpointCall(ScandServiceManager& serviceManager, const TestTarget& target, const nlohmann::json& input,
                         const std::string& key, std::uint32_t seqNo, std::shared_ptr<nlohmann::json> out);

    // Answers mgmtd's request. seqNo is the ticket mgmtd is holding the browser on.
    void sendTestResponse(ScandServiceManager& serviceManager, std::uint32_t seqNo, const nlohmann::json& out);

    // Hands the issued key to engined, the only database writer. The secret is sealed here so
    // the plaintext never crosses the IPC socket.
    void sendApiKeyState(ScandServiceManager& serviceManager, const std::string& keyOid, const std::string& key,
                         bool ok, const std::string& note);

    std::vector<AuthProfile> m_profiles;
    std::vector<ApiConnector> m_connectors;
};

}
