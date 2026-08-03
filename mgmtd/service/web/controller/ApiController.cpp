#include "service/web/controller/ApiController.h"

#include "service/MgmtdServiceManager.h"

#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

#include "db/Database.h"
#include "http/HttpMessage.h"
#include "http/UrlEncode.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace pz::mgmtd
{

using json = nlohmann::json;

namespace
{

// ── API connector tests ──────────────────────────────────────────────────────
// Two gated checks the operator runs while creating a connector:
//   1. keygen   — do these credentials produce an API key on this device?
//   2. endpoint — does the path the operator typed answer with that key?
// Both are separate because pretzel manages many customers' firewalls: the operator supplies
// the endpoint explicitly (PAN-OS versions differ per customer, and the REST path carries the
// version), so a working credential and a working path are independent failures worth
// distinguishing.
//
// Neither call happens here. collectord owns the device exchange: it is the daemon that will poll
// these connectors on a schedule, and a test exercising a different code path than the
// collector would not be testing much. mgmtd validates the request, forwards the target — which
// the operator may not have committed yet, so the whole thing travels in the payload — and
// correlates collectord's reply by seqNo, the same shape as the SAML ACS delegation to authd. The
// browser still polls /api/connector/test-result.

// The path the device is actually asked for: the endpoint plus its query parameters, encoded the
// same way collectord builds the request (ApiService::runEndpointCall). The endpoint-page test carries
// its parameters as a separate array, so the bare `endpoint` field understates what was called —
// this reunites them so the log shows the full request the operator entered. The connector test
// already bakes its parameters into `endpoint` and sends an empty array, so both flows log the
// same shape.
std::string fullEndpoint(const json& input)
{
    std::string path = input.value("endpoint", std::string());
    for (const auto& p : input.value("params", json::array()))
    {
        if (!p.is_object())
            continue;
        const std::string name = p.value("name", std::string());
        if (name.empty())
            continue;
        path += (path.find('?') == std::string::npos) ? '?' : '&';
        path += pz::http::urlEncode(name) + "=" + pz::http::urlEncode(p.value("value", std::string()));
    }
    return path;
}

// Rejects what can be judged without touching the device, so a typo answers immediately
// instead of costing an IPC round trip and a ticket poll. collectord re-checks defensively.
std::string connectorTestInputError(const json& input, bool endpointMode)
{
    if (input.value("target", std::string()).empty())
        return "target is required";

    // A password is only one of two ways in. collectord may already hold the key issued for this
    // profile, in which case it needs no credential at all — and only collectord knows that, so the
    // check here is "is there ANY way to authenticate", not "is there a password".
    //
    // is_object() is checked rather than assumed: nlohmann's value() throws when called on a
    // non-object, and a malformed body must produce a 400, not an exception.
    const auto secrets = input.value("secrets", json::object());
    const bool haveCredential = secrets.is_object() && !secrets.value("username", std::string()).empty() &&
                                !secrets.value("password", std::string()).empty();
    const bool namesProfile = !input.value("api_key_oid", std::string()).empty();

    // Generating a key needs a password somewhere, but not necessarily in this request: once saved,
    // the credential is sealed on the appliance and collectord opens its own copy, so a browser that
    // never typed it can still run the test. `stored_credential` says the caller knows of one; if it
    // turns out to be missing, collectord answers with the same complaint.
    const bool storedCredential = input.value("stored_credential", false);
    if (!endpointMode && !haveCredential && !storedCredential)
        return "generating a key needs the username and password — enter them on the API Key page";

    if (endpointMode && !haveCredential && !namesProfile)
        return "this test has no API key to use — pick one on the connector, or enter the "
               "password on the API Key page and generate a key once";

    if (!endpointMode)
        return {};

    const std::string endpoint = input.value("endpoint", std::string());
    if (endpoint.empty() || endpoint.front() != '/')
        return "endpoint must be a path starting with /";

    const std::string apiType = input.value("api_type", std::string("rest"));
    if (apiType != "xml" && apiType != "rest")
        return "api_type must be xml or rest";

    // Query parameters are entered as name/value pairs rather than typed into the path, so the
    // operator does not have to hand-encode them (PAN-OS REST requires ?location=, and XML API
    // cmd= values contain characters that must be escaped).
    if (!input.value("params", json::array()).is_array())
        return "params must be an array of {name, value}";

    return {};
}

// One cmd per operation (keygen / endpoint / sase): collectord routes each straight to its owning
// controller, so the operation no longer travels as a `mode` field in the payload.
void sendConnectorTest(MgmtdServiceManager& sm, std::uint32_t ticket, json input, pz::ipc::IpcCmd cmd)
{
    const std::string payload = input.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Collectord);
    msg->setCmd(cmd);
    msg->setSeqNo(ticket);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

}

void ApiController::keygenTest(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    json input;
    try
    {
        input = json::parse(req.body);
        LOG_INFO("handle keygen test json dump : {}", input.dump());
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    if (const auto err = connectorTestInputError(input, false); !err.empty())
    {
        LOG_DEBUG("keygen test rejected pre-flight (host={}, reason={})",
                  input.value("target", std::string()), err);
        return fill(resp, 400, json{{"error", err}}.dump());
    }

    const std::uint32_t ticket = sm.nextApiTestTicket();
    const std::string host = input.value("target", std::string());
    sendConnectorTest(sm, ticket, std::move(input), pz::ipc::IpcCmd::ApiKeygenRequest);

    LOG_INFO("keygen test delegated to collectord (ticket={}, host={})", ticket, host);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void ApiController::endpointTest(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    if (const auto err = connectorTestInputError(input, true); !err.empty())
    {
        LOG_DEBUG("endpoint test rejected pre-flight (host={}, endpoint={}, reason={})",
                  input.value("target", std::string()), fullEndpoint(input), err);
        return fill(resp, 400, json{{"error", err}}.dump());
    }

    const std::uint32_t ticket = sm.nextApiTestTicket();
    const std::string host = input.value("target", std::string());
    // The full request as entered on the frontend: path + query parameters, not just the bare path.
    const std::string endpoint = fullEndpoint(input);
    sendConnectorTest(sm, ticket, std::move(input), pz::ipc::IpcCmd::ApiEndpointTestRequest);

    LOG_INFO("endpoint test delegated to collectord (ticket={}, host={}, endpoint={})", ticket, host, endpoint);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

// SASE device health test: validate the getPrismaAccessIP api-key against the tenant and, on
// success, persist it (probed seals it and hands it to engined). Same ticket/poll shape as the
// connector tests — probed runs the slow call and answers on the seqNo.
void ApiController::saseTest(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    // No api-key requirement: the browser only holds the plaintext right after it was pasted, and
    // collectord falls back to the key stored for this device.
    if (input.value("oid", std::string()).empty() || input.value("url", std::string()).empty())
        return fill(resp, 400, R"({"error":"the device and probe URL are both required"})");

    const std::uint32_t ticket = sm.nextApiTestTicket();
    sendConnectorTest(sm, ticket, std::move(input), pz::ipc::IpcCmd::ApiSaseTestRequest);
    LOG_INFO("sase health test delegated to probed (ticket={})", ticket);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

// Persist a SASE device's health api-key. Saving it is its own operation, not a side effect of a
// passing test: the operator pastes the key while creating the device, and it must survive a save
// that was never tested — and a browser refresh, which is why the key goes to the appliance rather
// than living in the page. collectord seals it (it holds credentials.key; mgmtd never does) and
// engined stores it in sase_device.api_key_enc. It never enters running_config.
void ApiController::saseKeyStore(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    if (input.value("oid", std::string()).empty() || input.value("api_key", std::string()).empty())
        return fill(resp, 400, R"({"error":"the device and api-key are both required"})");

    const std::uint32_t ticket = sm.nextApiTestTicket();
    const std::string oid = input.value("oid", std::string());
    sendConnectorTest(sm, ticket, std::move(input), pz::ipc::IpcCmd::ApiSaseKeyStoreRequest);

    LOG_INFO("sase api-key store delegated to collectord (ticket={}, oid={})", ticket, oid);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

// Persist an API Key's account credential (username/password). Same reasoning as the SASE api-key:
// a password typed in one browser is invisible to every other browser and machine, so it goes to the
// appliance on save rather than living in localStorage until some test happens to pass. collectord
// seals it; engined stores it in api_credential_state. It never enters running_config.
void ApiController::credentialStore(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    const auto secrets = input.value("secrets", json::object());
    const bool havePassword = secrets.is_object() && !secrets.value("password", std::string()).empty();
    if (input.value("oid", std::string()).empty() || !havePassword)
        return fill(resp, 400, R"({"error":"the API Key and its password are both required"})");

    const std::uint32_t ticket = sm.nextApiTestTicket();
    const std::string oid = input.value("oid", std::string());
    sendConnectorTest(sm, ticket, std::move(input), pz::ipc::IpcCmd::ApiCredentialStoreRequest);

    LOG_INFO("api credential store delegated to collectord (ticket={}, oid={})", ticket, oid);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

// TLS-only fingerprint probe for an NGFW device: no credentials, just a handshake so the operator can
// pin the certificate when the device is created (before any API Key exists). Same ticket/poll shape
// as the other tests — collectord runs the handshake and answers on the seqNo.
void ApiController::tlsProbe(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    if (input.value("target", std::string()).empty())
        return fill(resp, 400, R"({"error":"target is required"})");

    const std::uint32_t ticket = sm.nextApiTestTicket();
    const std::string host = input.value("target", std::string());
    sendConnectorTest(sm, ticket, std::move(input), pz::ipc::IpcCmd::ApiTlsProbeRequest);

    LOG_INFO("tls probe delegated to collectord (ticket={}, host={})", ticket, host);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void ApiController::testResult(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    std::uint32_t ticket = 0;
    if (auto pos = req.target.find("ticket="); pos != std::string::npos)
        ticket = static_cast<std::uint32_t>(std::strtoul(req.target.c_str() + pos + 7, nullptr, 10));

    if (ticket == 0)
        return fill(resp, 400, R"({"error":"bad ticket"})");

    auto result = sm.takeApiTestResult(ticket);
    if (!result)
        return fill(resp, 200, R"({"status":"pending"})");

    // The worker stored a complete result document; tag it and hand it over as-is.
    json body = json::parse(*result, nullptr, false);
    if (body.is_discarded())
        return fill(resp, 500, R"({"status":"done","ok":false,"message":"malformed test result"})");

    body["status"] = "done";
    fill(resp, 200, body.dump());
}

// The shared, session-independent view of every API key's runtime state: whether a key/credential
// is held, expiry, and the last test outcome. Read straight from api_credential_state (engined is the
// writer, mgmtd may read), keyed by the API Key oid. Sealed blobs (key_enc/id_enc/pw_enc) are
// NEVER returned — only whether they exist — so the plaintext stays in the one process that holds it.
void ApiController::keysState(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    (void)req;

    json out = json::object();
    try
    {
        const auto rows = pz::db::Database::instance().queryRows(
            "SELECT oid, (key_enc IS NOT NULL)::int, (id_enc IS NOT NULL AND pw_enc IS NOT NULL)::int, "
            "COALESCE(to_char(expires_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
            "COALESCE(to_char(last_test_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
            "COALESCE(last_test_ok::int::text, ''), COALESCE(last_test_note, '') "
            "FROM api_credential_state");

        for (const auto& r : rows)
        {
            if (r.size() < 7 || r[0].empty())
                continue;
            json e;
            e["stored"] = (r[1] == "1");
            e["has_credential"] = (r[2] == "1");
            if (!r[3].empty())
                e["expires_at"] = r[3];
            if (!r[4].empty())   // a test has run
                e["last_test"] = {{"at", r[4]}, {"ok", r[5] == "1"}, {"detail", r[6]}};
            e["kind"] = "api_key";
            out[r[0]] = std::move(e);
        }

        // SASE devices carry their own health api-key (sase_device.api_key_enc) rather than an API Key
        // record, so they are keyed here by DEVICE oid — distinguishable by `kind`. Only whether a key
        // exists is reported: that is what lets the device editor render a saved key as bullets
        // instead of asking for it again after a refresh.
        for (const auto& r : pz::db::Database::instance().queryRows(
                 "SELECT oid, (api_key_enc IS NOT NULL)::int FROM sase_device"))
        {
            if (r.size() < 2 || r[0].empty())
                continue;
            out[r[0]] = {{"stored", r[1] == "1"}, {"kind", "sase_device"}};
        }
    }
    catch (const std::exception& ex)
    {
        LOG_WARN("keys-state query failed: {}", ex.what());
    }

    fill(resp, 200, out.dump());
}

}
