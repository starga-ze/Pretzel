#include "service/api/ApiService.h"

#include "service/CollectordServiceManager.h"
#include "service/api/ApiAction.h"
#include "service/api/ApiEvent.h"
#include "service/api/controller/ConnectorController.h"
#include "service/api/controller/ConnectorTest.h"
#include "service/api/controller/StatusController.h"
#include "service/api/ApiUtil.h"

#include "config/Config.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"
#include "util/Secret.h"

#include <chrono>
#include <memory>

#include <nlohmann/json.hpp>

namespace pz::collectord
{

using json = nlohmann::json;

namespace
{

const json& apiConfig()
{
    return pz::config::Config::serviceSection("collectord", "api");
}

}

void ApiService::loadProfiles(const nlohmann::json& cfg)
{
    const auto it = cfg.find("api_credentials");
    if (it == cfg.end() || !it->is_array())
        return;

    for (const auto& p : *it)
    {
        if (!p.is_object())
            continue;

        AuthProfile profile;
        // `uuid`/`id` = legacy keys, from before the single-identity merge.
        profile.oid = p.value("oid", p.value("uuid", p.value("id", std::string())));
        profile.name = p.value("name", std::string());
        profile.vendor = p.value("vendor", std::string());
        profile.username = p.value("username", std::string());
        profile.tls = p.value("tls", std::string("pin"));
        profile.fingerprint = p.value("fingerprint", std::string());
        profile.device = p.value("device", std::string());
        profile.endpoint = p.value("endpoint", std::string());
        profile.refreshMode = p.value("refresh_mode", std::string("manual"));
        profile.refreshIntervalMin = p.value("refresh_interval_min", 60);

        if (profile.oid.empty())
        {
            LOG_WARN("skipping auth profile without oid (name={})", profile.name);
            continue;
        }

        m_profiles.push_back(std::move(profile));
    }
}

void ApiService::loadEndpoints(const nlohmann::json& cfg)
{
    const auto it = cfg.find("endpoints");
    if (it == cfg.end() || !it->is_array())
        return;

    for (const auto& e : *it)
    {
        if (!e.is_object())
            continue;

        ApiEndpoint endpoint;
        endpoint.oid = e.value("oid", e.value("uuid", std::string()));   // `uuid` = legacy key
        endpoint.name = e.value("name", std::string());
        endpoint.path = e.value("path", std::string());

        // The REST path is the one that carries a version, so it is also the one that says
        // which API this is. Stated explicitly when present, derived otherwise.
        const std::string apiType =
            e.value("api_type", endpoint.path.rfind("/restapi/", 0) == 0 ? std::string("rest") : std::string("xml"));
        endpoint.apiType = (apiType == "xml") ? ApiType::Xml : ApiType::Rest;

        const auto params = e.find("params");
        if (params != e.end() && params->is_array())
        {
            for (const auto& p : *params)
            {
                if (!p.is_object())
                    continue;
                ApiParam param;
                param.name = p.value("name", std::string());
                param.value = p.value("value", std::string());
                if (!param.name.empty())
                    endpoint.params.push_back(std::move(param));
            }
        }

        if (endpoint.oid.empty())
        {
            LOG_WARN("skipping endpoint without oid (name={})", endpoint.name);
            continue;
        }

        if (endpoint.path.empty() || endpoint.path.front() != '/')
        {
            LOG_WARN("skipping endpoint with invalid path (name={}, path={})", endpoint.name, endpoint.path);
            continue;
        }

        m_endpoints.push_back(std::move(endpoint));
    }
}

void ApiService::loadConnectors(const nlohmann::json& cfg)
{
    const auto it = cfg.find("connectors");
    if (it == cfg.end() || !it->is_array())
        return;

    for (const auto& c : *it)
    {
        if (!c.is_object())
            continue;

        ApiConnector connector;
        connector.oid = c.value("oid", c.value("uuid", std::string()));   // `uuid` = legacy key
        connector.name = c.value("name", std::string());
        connector.objectOid = c.value("object", std::string());
        connector.authProfileOid = c.value("auth_profile", std::string());
        // Each item is one endpoint on its own schedule. `endpoint`/`params`/`poll_interval_sec`
        // at the connector level is the pre-policy shape, when a connector meant a single call;
        // such a connector is refused rather than half-read, so a stalled collection is visible.
        if (c.contains("endpoint") || c.contains("params"))
        {
            LOG_WARN("connector still has the single-endpoint shape — rebuild it as a list of "
                     "collected endpoints (name={})",
                     connector.name);
            continue;
        }

        const auto items = c.find("items");
        if (items != c.end() && items->is_array())
        {
            for (const auto& i : *items)
            {
                if (!i.is_object())
                    continue;

                ApiCollectionItem item;
                item.endpointOid = i.value("endpoint", std::string());
                item.pollIntervalSec = i.value("poll_interval_sec", std::int64_t{60});
                item.enabled = i.value("enabled", true);

                if (item.endpointOid.empty())
                {
                    LOG_WARN("skipping collection item without an endpoint (connector={})", connector.name);
                    continue;
                }

                if (item.pollIntervalSec < 1)
                {
                    LOG_WARN("collection item has a non-positive interval, using 60s "
                             "(connector={}, endpoint={})",
                             connector.name, item.endpointOid);
                    item.pollIntervalSec = 60;
                }

                connector.items.push_back(std::move(item));
            }
        }

        if (connector.oid.empty() || connector.objectOid.empty() || connector.authProfileOid.empty())
        {
            LOG_WARN("skipping connector without oid/object/auth_profile (name={})", connector.name);
            continue;
        }

        if (!findProfile(connector.authProfileOid))
        {
            LOG_WARN("connector references unknown auth profile (name={}, auth_profile={})", connector.name,
                     connector.authProfileOid);
        }

        if (connector.items.empty())
        {
            LOG_WARN("connector collects nothing — no endpoints listed (name={})", connector.name);
        }

        // Warned about, not dropped: mgmtd refuses to commit a dangling reference, so reaching
        // this means the config was edited outside the UI. Keeping the connector visible beats
        // making it disappear; the collector skips the item it cannot resolve.
        for (const auto& item : connector.items)
        {
            if (!findEndpoint(item.endpointOid))
            {
                LOG_WARN("connector references unknown endpoint (name={}, endpoint={})", connector.name,
                         item.endpointOid);
            }
        }

        m_connectors.push_back(std::move(connector));
    }
}

void ApiService::start()
{
    m_profiles.clear();
    m_endpoints.clear();
    m_connectors.clear();

    const auto& cfg = apiConfig();

    // Order matters: connectors resolve their endpoint and auth-profile references as they load.
    loadProfiles(cfg);
    loadEndpoints(cfg);
    loadConnectors(cfg);

    LOG_INFO("api service started (auth_profiles={}, endpoints={}, connectors={})", m_profiles.size(),
             m_endpoints.size(), m_connectors.size());
}

const std::vector<AuthProfile>& ApiService::profiles() const
{
    return m_profiles;
}

const AuthProfile* ApiService::findProfile(const std::string& oid) const
{
    for (const auto& p : m_profiles)
    {
        if (p.oid == oid)
            return &p;
    }
    return nullptr;
}

const std::vector<ApiEndpoint>& ApiService::endpoints() const
{
    return m_endpoints;
}

const ApiEndpoint* ApiService::findEndpoint(const std::string& oid) const
{
    for (const auto& e : m_endpoints)
    {
        if (e.oid == oid)
            return &e;
    }
    return nullptr;
}

const std::vector<ApiConnector>& ApiService::connectors() const
{
    return m_connectors;
}

// ── Issued-key cache ──────────────────────────────────────────────────────────────────────

// collectord consumes only the issued keys (to authenticate collection calls); keygen, credential
// re-issue and the connector test all moved to probed, which owns the credential lifecycle.
void ApiService::handleEvent(CollectordServiceManager& serviceManager, const ApiEvent& event)
{
    route(serviceManager, event);
}

void ApiService::handleAction(CollectordServiceManager& serviceManager, const ApiAction& action)
{
    switch (action.type())
    {
    case ApiActionType::RequestKeys:
        requestKeys(serviceManager);
        break;

    default:
        LOG_DEBUG("unhandled api action (type={})", static_cast<std::uint32_t>(action.type()));
        break;
    }
}

void ApiService::route(CollectordServiceManager& sm, const ApiEvent& event)
{
    std::uint32_t seqNo = 0;
    json input;

    switch (event.type())
    {
    // Connector tests: decode the payload, then hand straight to the owning controller. The service
    // stays out of the operation itself — no mode branch, no orchestration.
    case ApiEventType::RunKeygenTest:
        if (decodeTest(event, seqNo, input))
            m_credentialController.runKeygenTest(*this, sm, seqNo, input);
        break;

    case ApiEventType::RunEndpointTest:
        if (decodeTest(event, seqNo, input))
            m_endpointController.runEndpointTest(*this, sm, seqNo, input);
        break;

    case ApiEventType::RunSaseTest:
        if (decodeTest(event, seqNo, input))
            sm.statusController().runSaseTest(sm, seqNo, input);
        break;

    // Repo + scheduling: these stay in the service (shared key cache; when-to-run timing).
    case ApiEventType::ReceiveKeyState:
        handleKeyState(sm, event);
        break;

    case ApiEventType::Setup:
        handleSetup(sm, event);
        break;

    case ApiEventType::RunPeriodic:
        handleRunPeriodic(sm, event);
        break;

    default:
        LOG_DEBUG("unrouted api event (type={})", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

std::unique_ptr<CollectordEvent> ApiService::schedule(std::chrono::steady_clock::time_point now)
{
    using namespace std::chrono;

    // One-shot setup: fetch issued keys and arm periodic collection, once the IPC client is live.
    if (!m_setupDone)
    {
        m_setupDone = true;
        return std::make_unique<ApiEvent>(ApiEventType::Setup);
    }

    // Recurring housekeeping. Coarsely gated here (>=1s); the probe and auto-refresh keep their own
    // real intervals, so this only bounds how often the queue sees a RunPeriodic.
    if (m_lastPeriodicAt.time_since_epoch().count() == 0 || now - m_lastPeriodicAt >= seconds(1))
    {
        m_lastPeriodicAt = now;
        return std::make_unique<ApiEvent>(ApiEventType::RunPeriodic);
    }

    return nullptr;
}

// ── Event handlers ───────────────────────────────────────────────────────────────────────────

bool ApiService::decodeTest(const ApiEvent& event, std::uint32_t& seqNo, json& input) const
{
    const pz::ipc::IpcMessage* msg = event.message();
    if (!msg || msg->getPayload().empty())
    {
        LOG_WARN("empty connector-test request — dropping");
        return false;
    }
    seqNo = msg->getSeqNo();
    const auto& pl = msg->getPayload();
    try
    {
        input = json::parse(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse connector-test payload (error={})", e.what());
        return false;
    }
}

void ApiService::handleKeyState(CollectordServiceManager& sm, const ApiEvent& event)
{
    (void)sm;
    const pz::ipc::IpcMessage* msg = event.message();
    if (!msg || msg->getPayload().empty())
    {
        LOG_WARN("empty ApiCredentialStateResponse — dropping");
        return;
    }
    const auto& body = msg->getPayload();
    try
    {
        cacheKeys(json::parse(std::string(reinterpret_cast<const char*>(body.data()), body.size())));
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ApiCredentialStateResponse (error={})", e.what());
    }
}

void ApiService::handleSetup(CollectordServiceManager& sm, const ApiEvent& event)
{
    (void)event;
    // Fetch the issued keys (outbound IPC returned as an action), then arm periodic collection: each
    // item first fires after its interval, by when the keys have arrived; a poll with no key skips.
    sm.postAction(std::make_unique<ApiAction>(ApiActionType::RequestKeys));
    sm.connectorController().start(sm, *this);
}

void ApiService::handleRunPeriodic(CollectordServiceManager& sm, const ApiEvent& event)
{
    (void)event;
    const auto now = std::chrono::steady_clock::now();
    // SASE control-plane health probe + credential auto-refresh; each self-gates its real interval.
    sm.statusController().tick(now, *this, sm);
    autoRefreshTick(sm, now);
}

void ApiService::requestKeys(CollectordServiceManager& serviceManager)
{
    LOG_DEBUG("requesting issued api keys from engined");
    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Collectord);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::ApiCredentialStateRequest);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void ApiService::cacheKeys(const json& payload)
{
    const auto keys = payload.value("keys", json::array());
    if (!keys.is_array())
        return;

    std::unordered_map<std::string, std::string> opened;
    std::unordered_map<std::string, IssuedCredential> creds;
    std::size_t failed = 0;

    for (const auto& k : keys)
    {
        if (!k.is_object())
            continue;

        const std::string oid = k.value("oid", std::string());
        if (oid.empty())
            continue;

        // The issued key authenticates collection calls (and the SASE health probe, keyed by device
        // oid). A blob that will not open is a real condition, not a parse error: credentials.key was
        // replaced or lost. Say how many rather than which, so no oid/key pairing reaches a log.
        const std::string sealed = k.value("key_enc", std::string());
        if (!sealed.empty())
        {
            if (auto plain = pz::util::secret::decrypt(sealed))
                opened.emplace(oid, *plain);
            else
                ++failed;
        }

        // The durable account credential, for auto-refresh re-issue (owned by collectord since the
        // keygen/credential lifecycle moved here from probed).
        const std::string idEnc = k.value("id_enc", std::string());
        const std::string pwEnc = k.value("pw_enc", std::string());
        if (!idEnc.empty() && !pwEnc.empty())
        {
            auto id = pz::util::secret::decrypt(idEnc);
            auto pw = pz::util::secret::decrypt(pwEnc);
            if (id && pw)
                creds.emplace(oid, IssuedCredential{*id, *pw});
        }
    }

    m_issuedKeys = std::move(opened);
    m_credentials = std::move(creds);

    if (failed)
    {
        LOG_WARN("api keys received (usable={}, unreadable={}) — credentials.key may have changed; "
                 "re-run the key generation for those profiles",
                 m_issuedKeys.size(), failed);
    }
    else
    {
        LOG_INFO("api keys received (usable={})", m_issuedKeys.size());
    }
}

const std::string& ApiService::issuedKey(const std::string& authProfileOid) const
{
    static const std::string kNone;
    const auto it = m_issuedKeys.find(authProfileOid);
    return it == m_issuedKeys.end() ? kNone : it->second;
}

void ApiService::rememberIssuedKey(const std::string& authProfileOid, std::string key)
{
    m_issuedKeys[authProfileOid] = std::move(key);
}

const IssuedCredential* ApiService::credentialFor(const std::string& oid) const
{
    const auto it = m_credentials.find(oid);
    return it == m_credentials.end() ? nullptr : &it->second;
}

// Re-issue key/token for auto-refresh credentials whose interval has elapsed. Reuses the exact
// issuance path a manual keygen test takes (CredentialController::runKeygenTest) with a headless ticket (seqNo 0), so there is
// one code path for both. The account credential is opened from the cache engined fed us, and the
// bound device's target/type/fingerprint come from the running config.
void ApiService::autoRefreshTick(CollectordServiceManager& sm, std::chrono::steady_clock::time_point now)
{
    using namespace std::chrono;

    const auto& site = pz::config::Config::serviceSection("engined", "site");
    // A profile's device may be either kind, so both arrays are searched below; tag device_type
    // (dropped from config) back on so the keygen path can tell them apart.
    json devices = json::array();
    for (const char* key : {"ngfw_devices", "sase_devices"})
    {
        const std::string dt = std::string(key) == "sase_devices" ? "sase" : "ngfw";
        for (auto d : site.value(key, json::array()))
        {
            if (d.is_object())
                d["device_type"] = dt;
            devices.push_back(std::move(d));
        }
    }

    for (const auto& p : m_profiles)
    {
        if (p.refreshMode != "auto")
            continue;

        const auto interval = minutes(p.refreshIntervalMin > 0 ? p.refreshIntervalMin : 60);
        const auto it = m_lastRefresh.find(p.oid);
        if (it != m_lastRefresh.end() && now - it->second < interval)
            continue;

        // The re-issue needs the account credential; if it has not been cached yet (engined's
        // response still in flight), leave the timer unset so the next tick retries promptly.
        const IssuedCredential* cred = credentialFor(p.oid);
        if (!cred)
            continue;

        std::string target, deviceType = "ngfw", fingerprint;
        for (const auto& d : devices)
        {
            if (!d.is_object())
                continue;
            const std::string doid = d.value("oid", d.value("uuid", d.value("id", std::string())));
            if (doid == p.device)
            {
                target = d.value("target", std::string());
                deviceType = d.value("device_type", std::string()) == "sase" ? "sase" : "ngfw";
                fingerprint = d.value("fingerprint", std::string());
                break;
            }
        }
        if (target.empty())
        {
            m_lastRefresh[p.oid] = now;   // misconfigured (no device/target) — retry next interval
            continue;
        }

        m_lastRefresh[p.oid] = now;

        const json input = {
            {"oid", p.oid},
            {"mode", "keygen"},
            {"device_type", deviceType},
            {"target", target},
            {"fingerprint", fingerprint},
            {"endpoint", p.endpoint},
            {"secrets", {{"username", cred->id}, {"password", cred->pw}}},
        };

        LOG_INFO("auto-refresh re-issuing credential (oid={}, type={}, interval_min={})", p.oid, deviceType,
                 p.refreshIntervalMin);
        m_credentialController.runKeygenTest(*this, sm, 0, input);
    }
}

}
