#include "service/web/controller/ApiController.h"

#include "service/MgmtdServiceManager.h"

#include "service/web/WebUtil.h"
#include "service/web/WebRouter.h"

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

namespace
{

using json = nlohmann::json;

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

    // Generating a key is the one case a password is unavoidable — that exchange IS the
    // password. Calling an endpoint can instead ride on a key already issued for the profile.
    if (!endpointMode && !haveCredential)
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

void handleKeygenTest(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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

void handleEndpointTest(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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
void handleSaseTest(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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

    if (input.value("oid", std::string()).empty() || input.value("url", std::string()).empty() ||
        input.value("api_key", std::string()).empty())
        return fill(resp, 400, R"({"error":"the device, probe URL and api-key are all required"})");

    const std::uint32_t ticket = sm.nextApiTestTicket();
    sendConnectorTest(sm, ticket, std::move(input), pz::ipc::IpcCmd::ApiSaseTestRequest);
    LOG_INFO("sase health test delegated to probed (ticket={})", ticket);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void handleApiTestResult(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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
void handleKeysState(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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
            out[r[0]] = std::move(e);
        }
    }
    catch (const std::exception& ex)
    {
        LOG_WARN("keys-state query failed: {}", ex.what());
    }

    fill(resp, 200, out.dump());
}

}

void ApiController::registerRoutes(WebRouter& router)
{
    using Access = WebRouter::Access;

    router.post("/api/connector/keygen-test", Access::Authenticated, &handleKeygenTest);
    router.post("/api/connector/endpoint-test", Access::Authenticated, &handleEndpointTest);
    router.post("/api/connector/sase-test", Access::Authenticated, &handleSaseTest);
    router.getPrefix("/api/connector/test-result", Access::Authenticated, &handleApiTestResult);
    router.get("/api/connector/keys-state", Access::Authenticated, &handleKeysState);
}

}
