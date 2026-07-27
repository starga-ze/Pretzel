#include "service/api/ApiService.h"

#include "service/ScandServiceManager.h"
#include "service/api/ApiConnectorTester.h"
#include "service/api/ApiEvent.h"

#include "config/Config.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"
#include "util/Secret.h"

#include <nlohmann/json.hpp>

namespace pz::scand
{

using json = nlohmann::json;

namespace
{

const json& apiConfig()
{
    return pz::config::Config::serviceSection("scand", "api");
}

}

void ApiService::loadProfiles(const nlohmann::json& cfg)
{
    const auto it = cfg.find("api_keys");
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

// ── Connector test ──────────────────────────────────────────────────────────────────────

void ApiService::handleEvent(ScandServiceManager& serviceManager, const ApiEvent& event)
{
    const pz::ipc::IpcMessage* msg = event.message();

    if (event.type() == ApiEventType::ReceiveKeyState)
    {
        if (!msg || msg->getPayload().empty())
        {
            LOG_WARN("empty ApiKeyStateResponse — dropping");
            return;
        }
        const auto& body = msg->getPayload();
        try
        {
            cacheKeys(json::parse(std::string(reinterpret_cast<const char*>(body.data()), body.size())));
        }
        catch (const std::exception& e)
        {
            LOG_WARN("failed to parse ApiKeyStateResponse (error={})", e.what());
        }
        return;
    }

    if (event.type() != ApiEventType::ReceiveConnectorTestRequest)
    {
        return;
    }

    const pz::ipc::IpcMessage* in = event.message();
    if (!in || in->getPayload().empty())
    {
        LOG_WARN("empty ApiConnectorTestRequest — dropping");
        return;
    }

    const auto& pl = in->getPayload();
    json input;
    try
    {
        input = json::parse(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ApiConnectorTestRequest payload (error={})", e.what());
        return;
    }

    // The connector-test use case is its own unit: it reads the issued-key cache back through this
    // service and updates it via rememberIssuedKey, but owns the whole async device exchange. run()
    // is the backstop too — mgmtd is holding a browser on this seqNo, so no throw may escape here
    // and leave the ticket pending forever.
    ApiConnectorTester::run(*this, serviceManager, in->getSeqNo(), input);
}

void ApiService::requestKeys(ScandServiceManager& serviceManager)
{
    LOG_DEBUG("requesting issued api keys from engined");
    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Scand);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::ApiKeyStateRequest);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void ApiService::cacheKeys(const json& payload)
{
    const auto keys = payload.value("keys", json::array());
    if (!keys.is_array())
        return;

    std::unordered_map<std::string, std::string> opened;
    std::size_t failed = 0;

    for (const auto& k : keys)
    {
        if (!k.is_object())
            continue;

        const std::string oid = k.value("oid", std::string());
        const std::string sealed = k.value("secret_enc", std::string());
        if (oid.empty() || sealed.empty())
            continue;

        // A blob that will not open is a real condition, not a parse error: credentials.key was
        // replaced or lost. Say how many rather than which, so no oid/key pairing reaches a log.
        if (auto plain = pz::util::secret::decrypt(sealed))
            opened.emplace(oid, *plain);
        else
            ++failed;
    }

    m_issuedKeys = std::move(opened);

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

}
