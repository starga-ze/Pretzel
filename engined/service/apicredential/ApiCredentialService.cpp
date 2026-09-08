#include "service/apicredential/ApiCredentialService.h"

#include "service/EnginedServiceManager.h"

#include "config/Config.h"
#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::engined
{

namespace
{

// The two ids ai_route_credential_state may hold. The schema is the authority — its CHECK
// enumerates the same pair — and they are spelled out here rather than derived from the config,
// because a route row names the axis it needs, not the credential the axis draws on.
constexpr const char* kAirsCredentialId = "airs";
constexpr const char* kGatewayCredentialId = "portkey";

}

void ApiCredentialService::handleEvent(EnginedServiceManager& serviceManager, const ApiCredentialEvent& event)
{
    const pz::ipc::IpcMessage* in = event.message();

    if (event.type() == ApiCredentialEventType::ReceiveStateRequest)
    {
        // Both probed (credential lifecycle) and collectord (collection) request the issued keys, so
        // the response goes back to whoever asked, not a hard-coded daemon.
        const pz::ipc::IpcDaemon requester = in ? in->getSrc() : pz::ipc::IpcDaemon::Collectord;
        return sendState(serviceManager, requester, in ? in->getSeqNo() : 0);
    }

    if (event.type() == ApiCredentialEventType::ReceiveSaseApiKey)
    {
        if (in && !in->getPayload().empty())
        {
            const auto& pl = in->getPayload();
            storeSaseApiKey(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
        }
        return;
    }

    if (event.type() == ApiCredentialEventType::ReceiveAiCredential)
    {
        if (in && !in->getPayload().empty())
        {
            const auto& pl = in->getPayload();
            storeAiCredential(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
        }
        return;
    }

    if (event.type() != ApiCredentialEventType::ReceiveStateUpdate)
    {
        return;
    }

    if (!in || in->getPayload().empty())
    {
        LOG_WARN("empty ApiCredentialStateUpdate — dropping");
        return;
    }

    const auto& pl = in->getPayload();
    storeState(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
}

// probed validated a SASE device's health api-key (a getPrismaAccessIP read succeeded) and sealed it;
// store it on the device. Kept out of running_config, exactly like api_credential_state.
void ApiCredentialService::storeSaseApiKey(const std::string& payloadJson)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse SaseApiKeyUpdate payload (error={})", e.what());
        return;
    }

    const std::string oid = root.value("oid", "");
    const std::string keyEnc = root.value("api_key_enc", "");
    if (oid.empty() || keyEnc.empty())
    {
        LOG_WARN("SaseApiKeyUpdate without oid/api_key_enc — dropping");
        return;
    }

    // Upsert so a Test can save the key before the device projection exists; projectInventory fills
    // the config columns on the next reload and never touches api_key_enc, so both survive. A partial
    // row for a device that is never committed is pruned by projectInventory's DELETE.
    const bool wrote = pz::db::Database::instance().exec(
        "INSERT INTO sase_device (oid, api_key_enc) VALUES ($1, $2) "
        "ON CONFLICT (oid) DO UPDATE SET api_key_enc = EXCLUDED.api_key_enc, updated_at = now()",
        {oid, keyEnc});
    if (wrote)
        LOG_INFO("sase device api-key stored (oid={})", oid);
    else
        LOG_WARN("sase device api-key write failed (oid={})", oid);
}

void ApiCredentialService::storeState(const std::string& payloadJson)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ApiCredentialStateUpdate payload (error={})", e.what());
        return;
    }

    const std::string oid = root.value("oid", "");
    if (oid.empty())
    {
        LOG_WARN("ApiCredentialStateUpdate without oid — dropping");
        return;
    }

    // The credential and key arrive already encrypted: collectord holds the plaintext only long enough
    // to seal them, so nothing crosses the IPC socket or reaches this process in the clear.
    const std::string idEnc = root.value("id_enc", "");
    const std::string pwEnc = root.value("pw_enc", "");
    const std::string secretEnc = root.value("key_enc", "");
    const std::string expiresAt = root.value("expires_at", "");
    // A plain credential save carries no test outcome, so it must not stamp one: the `ok` field is
    // what distinguishes "a test ran and this is its result" from "store these credentials". Without
    // it the last_test_* columns keep whatever the last real test left.
    const bool hasTest = root.contains("ok");
    const bool ok = root.value("ok", false);
    const std::string note = root.value("note", "");

    // A failed test must not erase a working key or credential, so each sealed column and the issue
    // time are only written when a new value actually arrived — hence COALESCE on the excluded value.
    const bool wrote = pz::db::Database::instance().exec(
        "INSERT INTO api_credential_state (oid, id_enc, pw_enc, key_enc, issued_at, expires_at, "
        "last_test_at, last_test_ok, last_test_note) "
        "VALUES ($1, NULLIF($2,''), NULLIF($3,''), NULLIF($4,''), CASE WHEN $4 <> '' THEN now() END, "
        "NULLIF($5,'')::timestamptz, CASE WHEN $8::boolean THEN now() END, "
        "CASE WHEN $8::boolean THEN $6::boolean END, CASE WHEN $8::boolean THEN $7 END) "
        "ON CONFLICT (oid) DO UPDATE SET "
        "id_enc = COALESCE(EXCLUDED.id_enc, api_credential_state.id_enc), "
        "pw_enc = COALESCE(EXCLUDED.pw_enc, api_credential_state.pw_enc), "
        "key_enc = COALESCE(EXCLUDED.key_enc, api_credential_state.key_enc), "
        "issued_at = COALESCE(EXCLUDED.issued_at, api_credential_state.issued_at), "
        "expires_at = COALESCE(EXCLUDED.expires_at, api_credential_state.expires_at), "
        "last_test_at = COALESCE(EXCLUDED.last_test_at, api_credential_state.last_test_at), "
        "last_test_ok = COALESCE(EXCLUDED.last_test_ok, api_credential_state.last_test_ok), "
        "last_test_note = COALESCE(EXCLUDED.last_test_note, api_credential_state.last_test_note), "
        "updated_at = now()",
        {oid, idEnc, pwEnc, secretEnc, expiresAt, ok ? "true" : "false", note, hasTest ? "true" : "false"});

    if (wrote)
        LOG_INFO("api key state stored (oid={}, test={}, key={}, cred={})", oid, hasTest ? (ok ? "ok" : "failed") : "none",
                 secretEnc.empty() ? "unchanged" : "new", (idEnc.empty() && pwEnc.empty()) ? "unchanged" : "new");
    else
        LOG_WARN("api_credential_state write failed (oid={})", oid);
}

// The sealed AI keys the committed configuration still references. Everything else in the two
// stores is dropped.
//
// A key is state, not configuration — that is why it is in its own table rather than in
// running_config. But it is state ABOUT something the configuration declares, and when that
// declaration goes the state has nothing left to be about: a vendor removed from the provider list
// leaves a sealed key nothing can spend, and the console cannot show it because it filters the
// credential endpoint by the same whitelist that no longer contains it. Invisible key material that
// no operator action can reach is the thing this exists to prevent.
//
// The same shape as ProbeService::projectInventory, and for the same reason: config declares the
// set, engined writes the table, and the delete is what keeps them the same set. Written as one
// statement per store with the surviving ids passed in, so the database does the comparison and
// there is no read-then-write window for a concurrent store to fall into.
//
// EVERY commit, not just the ones carrying a pretzel-ai change. The document this reads is the
// whole committed configuration, so a commit that touched nothing here simply finds nothing to do —
// and an appliance that drifted for any other reason (an interrupted publish, a hand-edited
// document, a restore) is brought back into line by the next commit rather than staying wrong until
// somebody happens to edit the right page.
void ApiCredentialService::pruneAiCredentials()
{
    const auto& ai = pz::config::Config::scopeConfig(pz::config::scope::kPretzelAi);
    auto& db = pz::db::Database::instance();

    // The vendors, by the id their credential row is keyed under.
    nlohmann::json providerIds = nlohmann::json::array();
    for (const auto& p : ai.value("providers", nlohmann::json::object())
                           .value("list", nlohmann::json::array()))
    {
        if (!p.is_object())
            continue;
        const std::string id = p.value("id", std::string());
        if (!id.empty())
            providerIds.push_back(id);
    }

    // The route's two subscriptions, each kept only while something still needs it. Which one a
    // row needs follows from the axis it sits on — the gateway key belongs to the TRANSPORT,
    // because that is what cannot connect without it, and the AIRS key to the INSPECTOR. The
    // console draws exactly this pair per row; the two must agree or a Publish would delete the key
    // the page had just told the operator it was keeping.
    bool needsAirs = false;
    bool needsGateway = false;
    for (const auto& r : ai.value("route", nlohmann::json::object())
                           .value("list", nlohmann::json::array()))
    {
        if (!r.is_object())
            continue;
        if (r.value("guardrail", std::string()) == "api_application")
            needsAirs = true;
        if (r.value("transport", std::string()) == "ai_gateway")
            needsGateway = true;
    }

    nlohmann::json routeIds = nlohmann::json::array();
    if (needsAirs)
        routeIds.push_back(kAirsCredentialId);
    if (needsGateway)
        routeIds.push_back(kGatewayCredentialId);

    const std::string providerKeep = providerIds.dump();
    const std::string routeKeep = routeIds.dump();

    if (!db.exec("DELETE FROM ai_provider_credential_state "
                 "WHERE id <> ALL(ARRAY(SELECT jsonb_array_elements_text($1::jsonb)))",
                 {providerKeep}))
        LOG_WARN("ai_provider_credential_state prune failed");

    if (!db.exec("DELETE FROM ai_route_credential_state "
                 "WHERE id <> ALL(ARRAY(SELECT jsonb_array_elements_text($1::jsonb)))",
                 {routeKeep}))
        LOG_WARN("ai_route_credential_state prune failed");

    LOG_DEBUG("ai credentials pruned (providers_kept={}, route_kept={})", providerKeep, routeKeep);
}

// mgmtd sealed one of the assistant's API keys and handed it over.
//
// Two stores, one path. `scope` picks between them: "provider" is the vendors' table, keyed by id —
// 'openai', 'google', 'anthropic' — one row per vendor, because a key is issued by the vendor and
// works for every model they serve; "route" is the AI Route page's pair — the scan service and the
// gateway — which the schema holds as singletons. They are the same shape and are written
// identically, so they share this handler rather than a second command that would differ only in a
// table name — but they are separate tables because a vendor row is one of a set an operator adds
// to, and each of these is a single fact about this appliance.
//
// An unknown scope is dropped rather than defaulted. Defaulting would write a scan-service key into
// the vendor table on a typo, where the next push would hand it to a model provider.
void ApiCredentialService::storeAiCredential(const std::string& payloadJson)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("malformed AiCredentialStateUpdate ({}) — dropping", e.what());
        return;
    }

    const std::string id = root.value("id", "");
    if (id.empty())
    {
        LOG_WARN("AiCredentialStateUpdate without id — dropping");
        return;
    }

    const std::string scope = root.value("scope", std::string("provider"));
    const char* table = nullptr;
    if (scope == "provider")
        table = "ai_provider_credential_state";
    else if (scope == "route")
        table = "ai_route_credential_state";
    else
    {
        LOG_WARN("AiCredentialStateUpdate with unknown scope '{}' (id={}) — dropping", scope, id);
        return;
    }

    // Removing a key is its own instruction, not an empty string: `key_enc` is COALESCEd below so
    // that a store carrying only a test result leaves the key alone, which means "" cannot also
    // mean "delete it". An operator taking a vendor out of service needs the key gone, not the row
    // silently keeping it.
    if (root.value("clear", false))
    {
        const bool cleared = pz::db::Database::instance().exec(
            std::string("UPDATE ") + table + " SET key_enc = NULL, last_test_at = NULL, "
            "last_test_ok = NULL, last_test_note = NULL, updated_at = now() WHERE id = $1",
            {id});
        if (cleared)
            LOG_INFO("ai {} credential cleared (id={})", scope, id);
        else
            LOG_WARN("{} clear failed (id={})", table, id);
        return;
    }

    const std::string keyEnc = root.value("key_enc", "");
    // Same distinction the device path draws: `ok` present means a test ran and this is its verdict;
    // absent means "just store the key" and the last_test_* columns keep what a real test left.
    const bool hasTest = root.contains("ok");
    const bool ok = root.value("ok", false);
    const std::string note = root.value("note", "");

    const std::string t(table);
    const bool wrote = pz::db::Database::instance().exec(
        "INSERT INTO " + t + " (id, key_enc, last_test_at, last_test_ok, last_test_note) "
        "VALUES ($1, NULLIF($2,''), CASE WHEN $5::boolean THEN now() END, "
        "CASE WHEN $5::boolean THEN $3::boolean END, CASE WHEN $5::boolean THEN $4 END) "
        "ON CONFLICT (id) DO UPDATE SET "
        "key_enc = COALESCE(EXCLUDED.key_enc, " + t + ".key_enc), "
        "last_test_at = COALESCE(EXCLUDED.last_test_at, " + t + ".last_test_at), "
        "last_test_ok = COALESCE(EXCLUDED.last_test_ok, " + t + ".last_test_ok), "
        "last_test_note = COALESCE(EXCLUDED.last_test_note, " + t + ".last_test_note), "
        "updated_at = now()",
        {id, keyEnc, ok ? "true" : "false", note, hasTest ? "true" : "false"});

    // The key is never logged, and neither is its length — that alone narrows a guess.
    if (wrote)
        LOG_INFO("ai {} credential stored (id={}, test={}, key={})", scope, id,
                 hasTest ? (ok ? "ok" : "failed") : "none", keyEnc.empty() ? "unchanged" : "new");
    else
        LOG_WARN("{} write failed (id={})", table, id);
}

void ApiCredentialService::sendState(EnginedServiceManager& serviceManager, pz::ipc::IpcDaemon requester,
                                     std::uint32_t seqNo)
{
    // key_enc leaves as it was stored. engined has no credentials.key and could not open it
    // anyway; the requester does that, so a plaintext key exists in exactly one process.
    // key_enc is the issued key/token; id_enc/pw_enc are the durable account credential that lets
    // the requester (collectord) re-issue on its own for auto-refresh. All stay sealed — engined has no
    // credentials.key. A row is worth sending if it holds either an issued key or a credential.
    const auto rows = pz::db::Database::instance().queryRows(
        "SELECT oid, COALESCE(key_enc, ''), COALESCE(to_char(expires_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
        "COALESCE(id_enc, ''), COALESCE(pw_enc, '') "
        "FROM api_credential_state WHERE key_enc IS NOT NULL OR (id_enc IS NOT NULL AND pw_enc IS NOT NULL)");

    nlohmann::json keys = nlohmann::json::array();
    for (const auto& row : rows)
    {
        if (row.size() < 5 || row[0].empty())
            continue;
        keys.push_back({{"oid", row[0]},
                        {"key_enc", row[1]},
                        {"expires_at", row[2]},
                        {"id_enc", row[3]},
                        {"pw_enc", row[4]}});
    }

    // SASE device health api-keys ride the same response, keyed by device oid, so probed's SaseProbe
    // can look one up by oid exactly like an issued key. They live in sase_device (not running_config).
    for (const auto& row : pz::db::Database::instance().queryRows(
             "SELECT oid, api_key_enc FROM sase_device WHERE api_key_enc IS NOT NULL"))
    {
        if (row.size() < 2 || row[0].empty() || row[1].empty())
            continue;
        keys.push_back({{"oid", row[0]}, {"key_enc", row[1]}});
    }

    const std::string payload = nlohmann::json{{"keys", keys}}.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Engined);
    msg->setDst(requester);
    msg->setCmd(pz::ipc::IpcCmd::ApiCredentialStateResponse);
    msg->setSeqNo(seqNo);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    serviceManager.txRouter().handleIpcMessage(std::move(msg));

    LOG_INFO("api key state sent (keys={})", keys.size());
}

}
