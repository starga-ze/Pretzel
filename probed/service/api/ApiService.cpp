#include "service/api/ApiService.h"

#include "service/ProbedServiceManager.h"
#include "service/api/ApiConnectorTester.h"
#include "service/api/ApiEvent.h"

#include "config/Config.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"
#include "util/Secret.h"

#include <nlohmann/json.hpp>

namespace pz::probed
{

using json = nlohmann::json;

namespace
{

// The credential slice lives in collectord's api section, alongside the connectors that reference
// it; probed reads it cross-section (as it already reads engined.service.site for device targets).
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

void ApiService::start()
{
    m_profiles.clear();

    loadProfiles(apiConfig());

    LOG_INFO("api service started (auth_profiles={})", m_profiles.size());
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

// ── Connector test ──────────────────────────────────────────────────────────────────────

void ApiService::handleEvent(ProbedServiceManager& serviceManager, const ApiEvent& event)
{
    const pz::ipc::IpcMessage* msg = event.message();

    if (event.type() == ApiEventType::ReceiveKeyState)
    {
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

void ApiService::requestKeys(ProbedServiceManager& serviceManager)
{
    LOG_DEBUG("requesting issued api keys from engined");
    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Probed);
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

        // A blob that will not open is a real condition, not a parse error: credentials.key was
        // replaced or lost. Say how many rather than which, so no oid/key pairing reaches a log.
        const std::string sealed = k.value("secret_enc", std::string());
        if (!sealed.empty())
        {
            if (auto plain = pz::util::secret::decrypt(sealed))
                opened.emplace(oid, *plain);
            else
                ++failed;
        }

        // The durable account credential, for auto-refresh re-issue.
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

const IssuedCredential* ApiService::credentialFor(const std::string& oid) const
{
    const auto it = m_credentials.find(oid);
    return it == m_credentials.end() ? nullptr : &it->second;
}

void ApiService::rememberIssuedKey(const std::string& authProfileOid, std::string key)
{
    m_issuedKeys[authProfileOid] = std::move(key);
}

}
