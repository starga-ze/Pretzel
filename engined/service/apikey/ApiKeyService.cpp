#include "service/apikey/ApiKeyService.h"

#include "service/EnginedServiceManager.h"

#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::engined
{

void ApiKeyService::handleEvent(EnginedServiceManager&, const ApiKeyEvent& event)
{
    if (event.type() != ApiKeyEventType::ReceiveStateUpdate)
    {
        return;
    }

    const pz::ipc::IpcMessage* in = event.message();
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

    // The secret arrives already encrypted: mgmtd holds the plaintext only long enough to seal
    // it, so it never crosses the IPC socket or reaches this process in the clear.
    const std::string secretEnc = root.value("secret_enc", "");
    const bool ok = root.value("ok", false);
    const std::string note = root.value("note", "");
    const std::string expiresAt = root.value("expires_at", "");

    // A failed test must not erase a working key, so the secret and issue time are only written
    // when a new one actually arrived — hence COALESCE on the excluded value.
    const bool wrote = pz::db::Database::instance().exec(
        "INSERT INTO api_key_state (oid, secret_enc, issued_at, expires_at, "
        "last_test_at, last_test_ok, last_test_note) "
        "VALUES ($1, NULLIF($2,''), CASE WHEN $2 <> '' THEN now() END, NULLIF($3,'')::timestamptz, "
        "now(), $4::boolean, $5) "
        "ON CONFLICT (oid) DO UPDATE SET "
        "secret_enc = COALESCE(EXCLUDED.secret_enc, api_key_state.secret_enc), "
        "issued_at = COALESCE(EXCLUDED.issued_at, api_key_state.issued_at), "
        "expires_at = COALESCE(EXCLUDED.expires_at, api_key_state.expires_at), "
        "last_test_at = EXCLUDED.last_test_at, last_test_ok = EXCLUDED.last_test_ok, "
        "last_test_note = EXCLUDED.last_test_note, updated_at = now()",
        {oid, secretEnc, expiresAt, ok ? "true" : "false", note});

    if (wrote)
        LOG_INFO("api key state stored (oid={}, ok={}, key={})", oid, ok, secretEnc.empty() ? "unchanged" : "new");
    else
        LOG_WARN("api_key_state write failed (oid={})", oid);
}

}
