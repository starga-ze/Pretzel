#pragma once

#include "http/HttpMessage.h"

namespace pz::ipc
{
class IpcMessage;
}

namespace pz::mgmtd
{

class MgmtdServiceManager;

// Configuration ▸ AI Gateway, the server half of it.
//
// The gateway credential is the operator's own subscription key, so it follows the same path a
// device credential does rather than the path the database password does: entered once in the
// browser, sealed by the daemon that will present it, stored as ciphertext, and never returned.
// mgmtd validates the request and hands it to inferd — it holds no credentials.key and so could
// not read the stored value even if it wanted to.
//
// What is NOT here: the model catalog, the system prompt, the bypass flags. Those are operator
// declarations that belong in running_config, where they are versioned, diffed before publish and
// exported with the rest of the configuration — the settings-commit path already carries them.
// This controller owns only the secret and the question "is one configured".
class GatewayController
{
public:
    // POST /api/gateway/credential  {id, api_key} → 202 {ticket, status:"pending"}
    //
    // Answers with a ticket rather than inline for the same reason ChatController::send does: the
    // seal-and-store round trip crosses two daemons, and responses here are built synchronously on
    // the loop every other daemon's IPC arrives on. The browser polls the connector test-result
    // endpoint, which already carries exactly this shape of outcome.
    void credentialStore(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/gateway/status → {configured, last_test_at, last_test_ok, last_test_note}
    //
    // Never returns the key, or its length, or any prefix of it. The page needs to render
    // "configured" versus "not configured" and the last verification — nothing about the secret
    // itself, which the operator already has.
    void status(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // inferd finished sealing and storing. Correlated by seqNo, which is the ticket the browser
    // is polling on — filed into the same store the connector tests use.
    void onCredentialStoreResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg);
};

}
