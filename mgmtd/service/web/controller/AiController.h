#pragma once

#include "http/HttpMessage.h"

namespace pz::ipc
{
class IpcMessage;
}

namespace pz::mgmtd
{

class MgmtdServiceManager;

// The AI assistant's vendor API keys — the half of Configuration ▸ AI Assistant that cannot be
// committed. Everything else on that page (which vendors are enabled, their endpoints, their model
// catalogs, how a turn is shaped) is ordinary configuration and travels the settings-commit path;
// a key cannot, because running_config is append-versioned, rendered verbatim in the review diff
// and written out by Save-to-file, so a key written there would be permanent and readable by every
// reviewer.
//
// So it takes the same route a device credential takes: the plaintext crosses the wire once, on the
// way in, is sealed here with /etc/pretzel/credentials.key and handed to engined — the only
// database writer — as ciphertext. It is never read back out to the browser; the console is told
// only whether a key is stored.
//
// Sealed HERE rather than delegated to a worker daemon, which is where the device path differs.
// collectord seals a device credential because collectord is the process that will present it to
// the firewall, so the plaintext lives in exactly one process. The assistant's peer is pretzel-ai,
// which is not on the IPC fabric, so there is no worker to delegate to — mgmtd is already the
// process holding the plaintext and sealing it anywhere else would only add a hop.
class AiController
{
public:
    // GET /api/ai/credentials — per vendor: whether a key is stored, and when it last changed.
    // Never the key itself.
    void credentials(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // POST /api/ai/credential — { id, api_key } to store one, { id, clear: true } to remove it.
    void credentialStore(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/ai/models — the model catalog, per vendor, as the vendors last listed it.
    //
    // Read straight from ai_provider_model (collectord fetches it, engined writes it, mgmtd may
    // read) rather than shipped inside the console's JavaScript, which is where it used to live and
    // where it could only be corrected by a release.
    void models(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // POST /api/ai/models/update — { provider } to re-fetch one vendor's list.
    //
    // Delegated to collectord for the reason the connector tests are: it owns every outbound vendor
    // call and is the only daemon with an io_context to run one on. Answers 202 with a ticket; the
    // browser polls the route below. No key in the body — the vendor's key is already sealed in
    // engined's store, and collectord opens it there.
    void modelsUpdate(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/ai/models/update-result?ticket= — what that refresh found, once it lands.
    void modelsUpdateResult(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // collectord's answer, filed under the ticket the browser is holding. Called from the WebIpcEvent
    // handler on AiModelUpdateResponse.
    void onModelsUpdateResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg);
};

}
