#include "service/api/controller/SaseController.h"

#include "service/CollectordServiceManager.h"
#include "service/api/ApiService.h"
#include "service/api/ApiUtil.h"
#include "service/api/controller/ConnectorTest.h"

#include "http/HttpClient.h"
#include "http/UrlEncode.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <strings.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace pz::collectord
{

namespace
{

using json = nlohmann::json;

// Same cap as the NGFW endpoint test and the collector: enough to confirm the call returned what the
// operator meant to collect, small enough that a broad list cannot become a megabyte in a modal.
constexpr std::size_t kMaxBody = 16000;

void onSaseEndpointResponse(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res)
{
    const std::uint32_t seqNo = ctx->seqNo;
    json& out = ctx->out;

    if (!res.tlsOk || !res.requestSent)
    {
        LOG_WARN("SASE endpoint test could not send (seq={}, error={})", seqNo,
                 res.error.empty() ? "request was not sent" : res.error);
        out["steps"]["endpoint"] = stepJson(false, res.error.empty() ? "request was not sent" : res.error);
        out["ok"] = false;
        out["message"] = res.error;
        return sendTestResponse(*ctx->sm, seqNo, out);
    }

    const bool truncated = res.body.size() > kMaxBody;
    const bool ok = (res.status == 200);

    if (ok)
        LOG_INFO("SASE endpoint test result (seq={}, status={}, bytes={})", seqNo, res.status, res.body.size());
    else
        LOG_WARN("SASE endpoint test result (seq={}, status={}, bytes={})", seqNo, res.status, res.body.size());

    // 424 is worth naming outright. It is the vendor's answer to a wrong x-panw-region and it says
    // "tenant not found", which reads as a broken tenant rather than a wrong header — an operator can
    // lose an afternoon to that.
    std::string detail = "HTTP " + std::to_string(res.status);
    if (!ok)
    {
        if (res.status == 424)
            detail += " — the tenant was not found in this region. Check the x-panw-region header.";
        else if (res.status == 401 || res.status == 403)
            detail += " — the token was rejected. Check the credential's scope for this tenant.";
        else
            detail += " — " + res.body.substr(0, 160);
    }

    out["steps"]["endpoint"] = stepJson(ok, detail);
    out["ok"] = ok;
    out["response"] = {{"status", res.status},
                       {"body", res.body.substr(0, kMaxBody)},
                       {"bytes", res.body.size()},
                       {"truncated", truncated}};
    out["message"] = ok ? "endpoint responded" : detail;

    sendTestResponse(*ctx->sm, seqNo, out);
}

// Build and fire the product call. The token is passed in rather than read here so the two ways of
// getting one — already issued, or minted for this test — converge on one call site.
void callSaseEndpoint(const std::shared_ptr<ConnectorTest>& ctx, const std::string& token)
{
    const json& input = ctx->input;
    json& out = ctx->out;

    const std::string host = input.value("host", std::string());
    const std::string endpoint = input.value("endpoint", std::string());

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

    pz::http::ClientRequest call;
    call.host = host;
    call.port = 443;
    // Palo Alto's cloud, not a customer's box: verify the chain and the hostname. There is no
    // fingerprint to pin here, so the trust-on-first-use step the NGFW path needs does not exist.
    call.verifyCa = true;
    call.method = "GET";
    call.target = path;
    call.timeout = std::chrono::seconds(30);
    call.headers.emplace_back("Authorization", "Bearer " + token);

    // Operator headers last, so an endpoint can override Accept and friends — but never the
    // Authorization the daemon just minted.
    json shownHeaders = json::array();
    for (const auto& h : input.value("headers", json::array()))
    {
        if (!h.is_object())
            continue;
        const std::string name = h.value("name", std::string());
        if (name.empty())
            continue;
        if (::strcasecmp(name.c_str(), "authorization") == 0)
        {
            LOG_WARN("ignoring an Authorization header on a SASE endpoint (seq={}) — the token is minted per call",
                     ctx->seqNo);
            continue;
        }
        const std::string value = h.value("value", std::string());
        call.headers.emplace_back(name, value);
        shownHeaders.push_back({{"name", name}, {"value", value}});
    }

    out["request"] = {{"method", "GET"},
                      {"url", "https://" + host + path},
                      {"headers", shownHeaders},
                      {"key_delivery", "Authorization: Bearer <redacted>"}};

    LOG_TRACE("SASE endpoint request (seq={}, GET https://{}{})", ctx->seqNo, host, path);

    pz::http::requestAsync(ctx->sm->ioContext(), std::move(call),
                           [ctx](pz::http::ClientResponse res) { onSaseEndpointResponse(ctx, std::move(res)); });
}

// No token was cached for this credential, so one is minted for this call only. Not persisted: an
// endpoint test is about the path, and the credential page owns token storage.
void onTokenForEndpoint(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res)
{
    if (!res.tlsOk || !res.requestSent)
    {
        ctx->out["steps"]["auth"] = stepJson(false, res.error.empty() ? "the token request was not sent" : res.error);
        ctx->out["ok"] = false;
        return sendTestResponse(*ctx->sm, ctx->seqNo, ctx->out);
    }

    if (res.status != 200)
    {
        ctx->out["steps"]["auth"] =
            stepJson(false, "the auth server answered HTTP " + std::to_string(res.status) +
                                (res.status == 401 ? " — check the client id and secret" : ""));
        ctx->out["ok"] = false;
        return sendTestResponse(*ctx->sm, ctx->seqNo, ctx->out);
    }

    std::string token;
    try
    {
        token = json::parse(res.body).value("access_token", std::string());
    }
    catch (const std::exception&)
    {
        // fall through to the empty-token branch
    }

    if (token.empty())
    {
        ctx->out["steps"]["auth"] = stepJson(false, "the auth server returned no access_token");
        ctx->out["ok"] = false;
        return sendTestResponse(*ctx->sm, ctx->seqNo, ctx->out);
    }

    ctx->out["steps"]["auth"] = stepJson(true, "token issued for this test");
    callSaseEndpoint(ctx, token);
}

}

void SaseController::runEndpointTest(ApiService& api, CollectordServiceManager& sm, std::uint32_t seqNo,
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

        const std::string host = input.value("host", std::string());
        const std::string endpoint = input.value("endpoint", std::string());

        LOG_DEBUG("SASE endpoint test (seq={}, tsg={}, host={}, api_key_oid={})", seqNo, t.host, host,
                  t.authProfileOid);

        // `target` is the tenant id (the token's scope), not an address — so it is required, but for
        // a different reason than the NGFW path requires its host.
        if (t.host.empty())
            return rejectTest(ctx, "the tenant (TSG id) is required");
        if (host.empty())
            return rejectTest(ctx, "host is required, e.g. api.sase.paloaltonetworks.com");
        if (endpoint.empty() || endpoint.front() != '/')
            return rejectTest(ctx, "endpoint must be a path starting with /");
        if (!input.value("params", json::array()).is_array())
            return rejectTest(ctx, "params must be an array of {name, value}");
        if (!input.value("headers", json::array()).is_array())
            return rejectTest(ctx, "headers must be an array of {name, value}");

        ctx->out["steps"] = json::object();
        // There is no TLS step to report the way the NGFW path does: a CA-verified call either
        // connects or fails as a transport error, with no pin to confirm and nothing for the
        // operator to decide. The step is marked done so the panel's three rows still line up.
        ctx->out["steps"]["tls"] = stepJson(true, "verified against the public CA chain");

        // Reuse the token already issued for this credential when there is one — the SASE token IS
        // what api_credential_state holds for a SASE profile, exactly as the key is for an NGFW one.
        const std::string stored = api.issuedKey(t.authProfileOid);
        if (!stored.empty())
        {
            ctx->out["steps"]["auth"] = stepJson(true, "using the token already issued for this credential");
            ctx->out["used_stored_key"] = true;
            return callSaseEndpoint(ctx, stored);
        }

        if (t.username.empty() || t.password.empty())
            return rejectTest(ctx, "no token has been issued for this credential yet, and its client secret "
                                   "is not available — enter it on the API Credential page and run the "
                                   "credential test once");

        const HostPath hp = splitHostPath(t.keygenEndpoint, kSaseAuthHost, kSaseTokenPath);
        pz::http::requestAsync(sm.ioContext(), buildOAuthTokenRequest(hp, t.username, t.password, t.host),
                               [ctx](pz::http::ClientResponse res) { onTokenForEndpoint(ctx, std::move(res)); });
    }
    catch (const std::exception& e)
    {
        LOG_WARN("SASE endpoint test failed to start (seq={}, error={})", seqNo, e.what());
        json out;
        out["ok"] = false;
        out["steps"] = json::object();
        out["message"] = std::string("test could not be started: ") + e.what();
        sendTestResponse(sm, seqNo, out);
    }
}

}
