#include "service/apicredential/ApiCredentialService.h"

#include "service/EnginedServiceManager.h"

#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::engined
{

void ApiCredentialService::handleEvent(EnginedServiceManager& serviceManager, const ApiCredentialEvent& event)
{
    const pz::ipc::IpcMessage* in = event.message();

    if (event.type() == ApiCredentialEventType::ReceiveStateRequest)
    {
        return sendState(serviceManager, in ? in->getSeqNo() : 0);
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
        "INSERT INTO api_credential_state (oid, id_enc, pw_enc, secret_enc, issued_at, expires_at, "
        "last_test_at, last_test_ok, last_test_note) "
        "VALUES ($1, NULLIF($2,''), NULLIF($3,''), NULLIF($4,''), CASE WHEN $4 <> '' THEN now() END, "
        "NULLIF($5,'')::timestamptz, now(), $6::boolean, $7) "
        "ON CONFLICT (oid) DO UPDATE SET "
        "id_enc = COALESCE(EXCLUDED.id_enc, api_credential_state.id_enc), "
        "pw_enc = COALESCE(EXCLUDED.pw_enc, api_credential_state.pw_enc), "
        "secret_enc = COALESCE(EXCLUDED.secret_enc, api_credential_state.secret_enc), "
        "issued_at = COALESCE(EXCLUDED.issued_at, api_credential_state.issued_at), "
        "expires_at = COALESCE(EXCLUDED.expires_at, api_credential_state.expires_at), "
        "last_test_at = EXCLUDED.last_test_at, last_test_ok = EXCLUDED.last_test_ok, "
        "last_test_note = EXCLUDED.last_test_note, updated_at = now()",
        {oid, idEnc, pwEnc, secretEnc, expiresAt, ok ? "true" : "false", note});

    if (wrote)
        LOG_INFO("api key state stored (oid={}, ok={}, key={}, cred={})", oid, ok,
                 secretEnc.empty() ? "unchanged" : "new", (idEnc.empty() && pwEnc.empty()) ? "unchanged" : "new");
    else
        LOG_WARN("api_credential_state write failed (oid={})", oid);
}

void ApiCredentialService::sendState(EnginedServiceManager& serviceManager, std::uint32_t seqNo)
{
    // secret_enc leaves as it was stored. engined has no credentials.key and could not open it
    // anyway; the requester does that, so a plaintext key exists in exactly one process.
    // secret_enc is the issued key/token; id_enc/pw_enc are the durable account credential that lets
    // the requester (scand) re-issue on its own for auto-refresh. All stay sealed — engined has no
    // credentials.key. A row is worth sending if it holds either an issued key or a credential.
    const auto rows = pz::db::Database::instance().queryRows(
        "SELECT oid, COALESCE(secret_enc, ''), COALESCE(to_char(expires_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
        "COALESCE(id_enc, ''), COALESCE(pw_enc, '') "
        "FROM api_credential_state WHERE secret_enc IS NOT NULL OR (id_enc IS NOT NULL AND pw_enc IS NOT NULL)");

    nlohmann::json keys = nlohmann::json::array();
    for (const auto& row : rows)
    {
        if (row.size() < 5 || row[0].empty())
            continue;
        keys.push_back({{"oid", row[0]},
                        {"secret_enc", row[1]},
                        {"expires_at", row[2]},
                        {"id_enc", row[3]},
                        {"pw_enc", row[4]}});
    }

    const std::string payload = nlohmann::json{{"keys", keys}}.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Engined);
    msg->setDst(pz::ipc::IpcDaemon::Scand);
    msg->setCmd(pz::ipc::IpcCmd::ApiCredentialStateResponse);
    msg->setSeqNo(seqNo);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    serviceManager.txRouter().handleIpcMessage(std::move(msg));

    LOG_INFO("api key state sent (keys={})", keys.size());
}

}
