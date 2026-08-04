#include "service/api/controller/NgfwController.h"

#include "service/CollectordServiceManager.h"
#include "service/api/ApiService.h"
#include "service/api/ApiUtil.h"
#include "service/api/controller/ConnectorTest.h"

#include "http/HttpClient.h"
#include "http/UrlEncode.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace pz::collectord
{

namespace
{

using json = nlohmann::json;

void onEndpointResponse(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res)
{
    const std::string apiType = ctx->input.value("subtype", ctx->input.value("api_type", std::string("rest")));
    const std::uint32_t seqNo = ctx->seqNo;
    json& out = ctx->out;

    if (!res.tlsOk || !res.requestSent)
    {
        LOG_WARN("endpoint test could not send (seq={}, error={})", seqNo,
                 res.error.empty() ? "request was not sent" : res.error);
        out["steps"]["endpoint"] = stepJson(false, res.error.empty() ? "request was not sent" : res.error);
        out["ok"] = false;
        out["message"] = res.error;
        return sendTestResponse(*ctx->sm, seqNo, out);
    }

    // The body is returned so the operator can confirm the path produced what they meant to
    // collect, not merely that it returned 200. Capped because a broad query would be megabytes.
    constexpr std::size_t kMaxBody = 16000;
    const bool truncated = res.body.size() > kMaxBody;
    const bool ok = (res.status == 200);

    if (ok)
        LOG_INFO("endpoint test result (seq={}, status={}, bytes={})", seqNo, res.status, res.body.size());
    else
        LOG_WARN("endpoint test result (seq={}, status={}, bytes={})", seqNo, res.status, res.body.size());

    out["steps"]["endpoint"] =
        stepJson(ok, "HTTP " + std::to_string(res.status) +
                         (ok ? "" : " — " + (apiType == "xml" ? xmlErrorMessage(res.body) : res.body.substr(0, 160))));
    out["ok"] = ok;
    out["response"] = {{"status", res.status},
                       {"body", res.body.substr(0, kMaxBody)},
                       {"bytes", res.body.size()},
                       {"truncated", truncated}};
    out["message"] = ok ? "endpoint responded" : "endpoint returned HTTP " + std::to_string(res.status);

    sendTestResponse(*ctx->sm, seqNo, out);
}

void callEndpoint(const std::shared_ptr<ConnectorTest>& ctx, const std::string& key)
{
    const TestTarget& target = ctx->target;
    const json& input = ctx->input;
    json& out = ctx->out;

    auto call = baseRequest(target);

    const std::string apiType = input.value("subtype", input.value("api_type", std::string("rest")));
    const std::string endpoint = input.value("endpoint", std::string());

    // Path + operator-supplied parameters, percent-encoded here so the operator can type raw values.
    std::string path = endpoint;
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

    call.target = path;

    // What the operator is shown as the request line — the key never appears in it.
    std::string displayTarget = path;

    // The two PAN-OS APIs carry the key differently: XML API as a query parameter, REST as a header.
    if (apiType == "xml")
    {
        const char sep = (path.find('?') == std::string::npos) ? '?' : '&';
        call.target += sep + std::string("key=") + pz::http::urlEncode(key);
        displayTarget += sep + std::string("key=<redacted>");
    }
    else
    {
        call.headers.emplace_back("X-PAN-KEY", key);
    }

    const std::string portPart = (target.port == 443) ? "" : ":" + std::to_string(target.port);
    const std::string displayUrl = "https://" + target.host + portPart + displayTarget;
    out["request"] = {{"method", "GET"},
                      {"url", displayUrl},
                      {"key_delivery", apiType == "xml" ? "key= query parameter" : "X-PAN-KEY header"}};

    LOG_TRACE("endpoint request (seq={}, GET {})", ctx->seqNo, displayUrl);

    pz::http::requestAsync(ctx->sm->ioContext(), std::move(call),
                           [ctx](pz::http::ClientResponse res) { onEndpointResponse(ctx, std::move(res)); });
}

// A key had to be issued first (none stored). It is used for this call only — an endpoint test is
// about the path, so it does not persist the key (unlike the keygen test).
void onKeygenForEndpoint(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res)
{
    const std::string key = readKeygenKey(res, !ctx->target.fingerprint.empty(), ctx->out);
    if (key.empty())
    {
        ctx->out["ok"] = false;
        return sendTestResponse(*ctx->sm, ctx->seqNo, ctx->out);
    }
    callEndpoint(ctx, key);
}

}

void NgfwController::runEndpointTest(ApiService& api, CollectordServiceManager& sm, std::uint32_t seqNo,
                                         const json& input)
{
    auto ctx = std::make_shared<ConnectorTest>();
    ctx->api = &api;
    ctx->sm = &sm;
    ctx->seqNo = seqNo;
    ctx->input = input;

    // Backstop: mgmtd holds a browser on this seqNo, so a throw must not leave the ticket pending.
    try
    {
        ctx->target = parseTestTarget(input);
        const TestTarget& t = ctx->target;

        LOG_DEBUG("endpoint test (seq={}, host={}, api_key_oid={})", seqNo, t.host, t.authProfileOid);

        if (t.host.empty())
            return rejectTest(ctx, "target is required");

        const std::string endpoint = input.value("endpoint", std::string());
        const std::string apiType = input.value("subtype", input.value("api_type", std::string("rest")));
        if (endpoint.empty() || endpoint.front() != '/')
            return rejectTest(ctx, "endpoint must be a path starting with /");
        if (apiType != "xml" && apiType != "rest")
            return rejectTest(ctx, "subtype must be xml or rest");
        if (!input.value("params", json::array()).is_array())
            return rejectTest(ctx, "params must be an array of {name, value}");

        ctx->out["steps"] = json::object();

        // Use the key already issued for this profile if there is one; otherwise issue a transient
        // one (keygen) and continue to the call.
        const std::string stored = api.issuedKey(t.authProfileOid);
        if (!stored.empty())
        {
            ctx->out["steps"]["auth"] = stepJson(true, "using the key already issued for this profile");
            ctx->out["used_stored_key"] = true;
            return callEndpoint(ctx, stored);
        }

        if (t.username.empty() || t.password.empty())
            return rejectTest(ctx, "no key has been issued for this API key yet, and its password is not "
                                   "available — enter the password on the API Key page and run the key "
                                   "generation once");

        pz::http::requestAsync(sm.ioContext(), buildKeygenRequest(t),
                               [ctx](pz::http::ClientResponse res) { onKeygenForEndpoint(ctx, std::move(res)); });
    }
    catch (const std::exception& e)
    {
        LOG_WARN("endpoint test failed to start (seq={}, error={})", seqNo, e.what());
        json out;
        out["ok"] = false;
        out["steps"] = json::object();
        out["message"] = std::string("test could not be started: ") + e.what();
        sendTestResponse(sm, seqNo, out);
    }
}

}
