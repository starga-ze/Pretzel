#include "service/web/controller/ConnectorController.h"

#include "service/MgmtdServiceManager.h"

#include "service/web/WebResponse.h"
#include "service/web/WebRouter.h"

#include "router/MgmtdTxRouter.h"

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
// Neither call happens here. scand owns the device exchange: it is the daemon that will poll
// these connectors on a schedule, and a test exercising a different code path than the
// collector would not be testing much. mgmtd validates the request, forwards the target — which
// the operator may not have committed yet, so the whole thing travels in the payload — and
// correlates scand's reply by seqNo, the same shape as the SAML ACS delegation to authd. The
// browser still polls /api/connector/test-result.

// The path the device is actually asked for: the endpoint plus its query parameters, encoded the
// same way scand builds the request (ApiService::runEndpointCall). The endpoint-page test carries
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
// instead of costing an IPC round trip and a ticket poll. scand re-checks defensively.
std::string connectorTestInputError(const json& input, bool endpointMode)
{
    if (input.value("target", std::string()).empty())
        return "target is required";

    // A password is only one of two ways in. scand may already hold the key issued for this
    // profile, in which case it needs no credential at all — and only scand knows that, so the
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

void sendConnectorTest(MgmtdServiceManager& sm, std::uint32_t ticket, json input, const char* mode)
{
    input["mode"] = mode;
    const std::string payload = input.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Scand);
    msg->setCmd(pz::ipc::IpcCmd::ApiConnectorTestRequest);
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
    sendConnectorTest(sm, ticket, std::move(input), "keygen");

    LOG_INFO("keygen test delegated to scand (ticket={}, host={})", ticket, host);
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
    sendConnectorTest(sm, ticket, std::move(input), "endpoint");

    LOG_INFO("endpoint test delegated to scand (ticket={}, host={}, endpoint={})", ticket, host, endpoint);
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

}

void ConnectorController::registerRoutes(WebRouter& router)
{
    using Access = WebRouter::Access;

    router.post("/api/connector/keygen-test", Access::Authenticated, &handleKeygenTest);
    router.post("/api/connector/endpoint-test", Access::Authenticated, &handleEndpointTest);
    router.getPrefix("/api/connector/test-result", Access::Authenticated, &handleApiTestResult);
}

}
