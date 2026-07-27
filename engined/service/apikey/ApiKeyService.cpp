#include "service/apikey/ApiKeyService.h"

#include "service/EnginedServiceManager.h"

#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::engined
{

void ApiKeyService::handleEvent(EnginedServiceManager& serviceManager, const ApiKeyEvent& event)
{
    const pz::ipc::IpcMessage* in = event.message();

    if (event.type() == ApiKeyEventType::ReceiveStateRequest)
    {
        return sendState(serviceManager, in ? in->getSeqNo() : 0);
    }

    if (event.type() != ApiKeyEventType::ReceiveStateUpdate)
    {
        return;
    }

    if (!in || in->getPayload().empty())
    {
        LOG_WARN("empty ApiKeyStateUpdate — dropping");
        return;
    }

    const auto& pl = in->getPayload();
    storeState(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
}

void ApiKeyService::storeState(const std::string& payloadJson)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ApiKeyStateUpdate payload (error={})", e.what());
        return;
    }

    const std::string oid = root.value("oid", "");
    if (oid.empty())
    {
        LOG_WARN("ApiKeyStateUpdate without oid — dropping");
        return;
    }

    // The credential and key arrive already encrypted: scand holds the plaintext only long enough
    // to seal them, so nothing crosses the IPC socket or reaches this process in the clear.
    const std::string idEnc = root.value("id_enc", "");
    const std::string pwEnc = root.value("pw_enc", "");
    const std::string secretEnc = root.value("secret_enc", "");
    const bool ok = root.value("ok", false);
    const std::string note = root.value("note", "");
    const std::string expiresAt = root.value("expires_at", "");

    // A failed test must not erase a working key or credential, so each sealed column and the issue
    // time are only written when a new value actually arrived — hence COALESCE on the excluded value.
    const bool wrote = pz::db::Database::instance().exec(
        "INSERT INTO api_key_state (oid, id_enc, pw_enc, secret_enc, issued_at, expires_at, "
        "last_test_at, last_test_ok, last_test_note) "
        "VALUES ($1, NULLIF($2,''), NULLIF($3,''), NULLIF($4,''), CASE WHEN $4 <> '' THEN now() END, "
        "NULLIF($5,'')::timestamptz, now(), $6::boolean, $7) "
        "ON CONFLICT (oid) DO UPDATE SET "
        "id_enc = COALESCE(EXCLUDED.id_enc, api_key_state.id_enc), "
        "pw_enc = COALESCE(EXCLUDED.pw_enc, api_key_state.pw_enc), "
        "secret_enc = COALESCE(EXCLUDED.secret_enc, api_key_state.secret_enc), "
        "issued_at = COALESCE(EXCLUDED.issued_at, api_key_state.issued_at), "
        "expires_at = COALESCE(EXCLUDED.expires_at, api_key_state.expires_at), "
        "last_test_at = EXCLUDED.last_test_at, last_test_ok = EXCLUDED.last_test_ok, "
        "last_test_note = EXCLUDED.last_test_note, updated_at = now()",
        {oid, idEnc, pwEnc, secretEnc, expiresAt, ok ? "true" : "false", note});

    if (wrote)
        LOG_INFO("api key state stored (oid={}, ok={}, key={}, cred={})", oid, ok,
                 secretEnc.empty() ? "unchanged" : "new", (idEnc.empty() && pwEnc.empty()) ? "unchanged" : "new");
    else
        LOG_WARN("api_key_state write failed (oid={})", oid);
}

void ApiKeyService::sendState(EnginedServiceManager& serviceManager, std::uint32_t seqNo)
{
    // secret_enc leaves as it was stored. engined has no credentials.key and could not open it
    // anyway; the requester does that, so a plaintext key exists in exactly one process.
    const auto rows = pz::db::Database::instance().queryRows(
        "SELECT oid, COALESCE(secret_enc, ''), COALESCE(to_char(expires_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), '') "
        "FROM api_key_state WHERE secret_enc IS NOT NULL");

    nlohmann::json keys = nlohmann::json::array();
    for (const auto& row : rows)
    {
        if (row.size() < 3 || row[0].empty() || row[1].empty())
            continue;
        keys.push_back({{"oid", row[0]}, {"secret_enc", row[1]}, {"expires_at", row[2]}});
    }

    const std::string payload = nlohmann::json{{"keys", keys}}.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Engined);
    msg->setDst(pz::ipc::IpcDaemon::Scand);
    msg->setCmd(pz::ipc::IpcCmd::ApiKeyStateResponse);
    msg->setSeqNo(seqNo);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    serviceManager.txRouter().handleIpcMessage(std::move(msg));

    LOG_INFO("api key state sent (keys={})", keys.size());
}

}
