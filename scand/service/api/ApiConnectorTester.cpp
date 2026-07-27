#include "service/api/ApiConnectorTester.h"

#include "service/ScandServiceManager.h"
#include "service/api/ApiService.h"

#include "http/HttpClient.h"
#include "http/UrlEncode.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"
#include "util/Secret.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pz::scand
{

namespace
{

using json = nlohmann::json;

// ── Connector test helpers ──────────────────────────────────────────────────────────────
//
// A test separates two failures the operator cannot otherwise tell apart: the credential is
// wrong, or the path is wrong. The device is reached twice — once to exchange the credential
// for a key, once to call the endpoint with it — and each step is reported on its own.

json stepJson(bool ok, std::string detail)
{
    return {{"ok", ok}, {"detail", std::move(detail)}};
}

TestTarget parseTestTarget(const json& body)
{
    TestTarget t;
    t.host = body.value("target", std::string());

    // A target may carry an explicit port ("10.0.0.1:8443"); IPv6 literals are bracketed.
    if (!t.host.empty() && t.host.front() != '[')
    {
        const auto colon = t.host.rfind(':');
        if (colon != std::string::npos && t.host.find(':') == colon)
        {
            try
            {
                t.port = static_cast<std::uint16_t>(std::stoi(t.host.substr(colon + 1)));
                t.host.erase(colon);
            }
            catch (const std::exception&)
            {
            }
        }
    }

    t.fingerprint = body.value("fingerprint", std::string());

    // is_object() is checked rather than assumed: nlohmann's value() throws when called on a
    // non-object, and a malformed payload must fail the test, not the daemon.
    const auto secrets = body.value("secrets", json::object());
    if (secrets.is_object())
    {
        t.username = secrets.value("username", body.value("username", std::string()));
        t.password = secrets.value("password", std::string());
    }
    else
    {
        t.username = body.value("username", std::string());
    }
    t.keygenEndpoint = body.value("keygen_endpoint", body.value("endpoint", std::string()));
    t.authProfileOid = body.value("api_key_oid", std::string());
    return t;
}

pz::http::ClientRequest baseRequest(const TestTarget& t)
{
    pz::http::ClientRequest req;
    req.host = t.host;
    req.port = t.port;
    req.expectedFingerprint = t.fingerprint;
    req.timeout = std::chrono::seconds(10);
    return req;
}

// Populate the tls step and say whether the caller may continue. A first contact or a pin
// mismatch stops here — HttpClient refuses to transmit credentials to an unverified peer.
bool recordTlsStep(const pz::http::ClientResponse& r, bool hadPin, json& out)
{
    out["fingerprint"] = r.fingerprint;
    out["cert_subject"] = r.certSubject;
    out["fingerprint_trusted"] = r.pinMatched;

    if (!r.tlsOk)
    {
        out["steps"]["tls"] = stepJson(false, r.error.empty() ? "TLS handshake failed" : r.error);
        out["message"] = r.error;
        return false;
    }

    if (!r.pinMatched)
    {
        const bool firstContact = !hadPin && !r.fingerprint.empty();
        out["steps"]["tls"] =
            stepJson(false, firstContact ? "certificate not yet trusted — confirm the fingerprint"
                                         : "certificate fingerprint does not match the pinned value");
        out["message"] = firstContact ? "confirm the device certificate to continue"
                                      : "certificate fingerprint mismatch — possible interception";
        return false;
    }

    out["steps"]["tls"] = stepJson(true, "TLS established, certificate pinned");
    return true;
}

// PAN-OS answers keygen with <response status="success"><result><key>…</key></result></response>.
std::string extractXmlTag(const std::string& xml, const std::string& tag)
{
    const std::string open = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    const auto b = xml.find(open);
    if (b == std::string::npos)
        return {};
    const auto s = b + open.size();
    const auto e = xml.find(close, s);
    if (e == std::string::npos)
        return {};
    return xml.substr(s, e - s);
}

std::string xmlErrorMessage(const std::string& xml)
{
    const auto msg = extractXmlTag(xml, "msg");
    return msg.empty() ? "device rejected the request" : msg;
}

// ── One in-flight test ───────────────────────────────────────────────────────────────────
// Created per request and threaded through the async stages as a shared_ptr so the state outlives
// each completion callback — the daemon loop keeps running while a slow or unreachable device is
// waited on. `api`/`sm` are back-pointers to objects that live for the process; `out` is the
// result document mgmtd relays to the browser.
struct ConnectorTest
{
    ApiService* api{nullptr};
    ScandServiceManager* sm{nullptr};
    std::uint32_t seqNo{0};
    TestTarget target;
    std::string mode;     // "keygen" | "endpoint"
    std::string keyOid;   // API Key record oid the issued key is persisted under (keygen mode)
    json input;           // original request payload — endpoint path/params are read back from here
    json out;             // the result document
};

// The stages call one another across async boundaries; forward-declared so they can be written in
// call order below.
void sendTestResponse(ScandServiceManager& sm, std::uint32_t seqNo, const json& out);
void sendApiKeyState(ScandServiceManager& sm, const std::string& keyOid, const std::string& key, bool ok,
                     const std::string& note);
void runConnectorTest(const std::shared_ptr<ConnectorTest>& ctx);
void rejectTest(const std::shared_ptr<ConnectorTest>& ctx, const std::string& message);
void afterKey(const std::shared_ptr<ConnectorTest>& ctx, const std::string& key);
void runKeygen(const std::shared_ptr<ConnectorTest>& ctx);
void onKeygenResponse(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res);
void runEndpointCall(const std::shared_ptr<ConnectorTest>& ctx, const std::string& key);
void onEndpointResponse(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res);

void runConnectorTest(const std::shared_ptr<ConnectorTest>& ctx)
{
    ctx->target = parseTestTarget(ctx->input);
    ctx->mode = ctx->input.value("mode", std::string("keygen"));
    ctx->keyOid = ctx->input.value("oid", std::string());

    const TestTarget& target = ctx->target;
    const json& input = ctx->input;

    LOG_DEBUG("connector test received (seq={}, mode={}, host={}, port={}, api_key_oid={})", ctx->seqNo, ctx->mode,
              target.host, target.port, target.authProfileOid);

    // Validation failures answer immediately — mgmtd is holding a browser on this ticket, so
    // every path has to produce exactly one response.
    if (target.host.empty())
        return rejectTest(ctx, "target is required");

    // A key already issued for this profile stands in for the password: it was sealed at
    // issuance and lives in api_key_state, so the operator types the password once rather than
    // once per browser session. Falling back to keygen keeps the first run — and a re-issue
    // after the key is rejected — working.
    const std::string stored = ctx->api->issuedKey(target.authProfileOid);

    if (stored.empty() && (target.username.empty() || target.password.empty()))
    {
        return rejectTest(ctx, "no key has been issued for this API key yet, and its password is not "
                               "available — enter the password on the API Key page and run the key "
                               "generation once");
    }

    if (ctx->mode == "endpoint")
    {
        const std::string endpoint = input.value("endpoint", std::string());
        const std::string apiType = input.value("api_type", std::string("rest"));
        if (endpoint.empty() || endpoint.front() != '/')
            return rejectTest(ctx, "endpoint must be a path starting with /");
        if (apiType != "xml" && apiType != "rest")
            return rejectTest(ctx, "api_type must be xml or rest");
        if (!input.value("params", json::array()).is_array())
            return rejectTest(ctx, "params must be an array of {name, value}");
    }

    ctx->out["steps"] = json::object();

    if (!stored.empty())
    {
        LOG_DEBUG("connector test using stored key (seq={}, api_key_oid={})", ctx->seqNo, target.authProfileOid);
        ctx->out["steps"]["auth"] = stepJson(true, "using the key already issued for this profile");
        ctx->out["used_stored_key"] = true;
        return afterKey(ctx, stored);
    }

    LOG_DEBUG("connector test has no stored key — running keygen (seq={}, host={})", ctx->seqNo, target.host);
    runKeygen(ctx);
}

void rejectTest(const std::shared_ptr<ConnectorTest>& ctx, const std::string& message)
{
    LOG_DEBUG("connector test rejected (seq={}, reason={})", ctx->seqNo, message);
    json out;
    out["ok"] = false;
    out["steps"] = json::object();
    out["message"] = message;
    sendTestResponse(*ctx->sm, ctx->seqNo, out);
}

// Continuation once a key is in hand (issued now, or already stored): run the endpoint call in
// endpoint mode, or report the keygen outcome.
void afterKey(const std::shared_ptr<ConnectorTest>& ctx, const std::string& key)
{
    if (ctx->mode == "endpoint")
    {
        if (key.empty())
        {
            ctx->out["ok"] = false;
            return sendTestResponse(*ctx->sm, ctx->seqNo, ctx->out);
        }
        return runEndpointCall(ctx, key);
    }

    // Keygen test: the issued key is persisted, and only the outcome — not the key — goes back
    // to the browser.
    ctx->out["ok"] = !key.empty();
    if (!key.empty())
        ctx->out["message"] = "API key generated";

    if (!ctx->keyOid.empty())
    {
        sendApiKeyState(*ctx->sm, ctx->keyOid, key, !key.empty(), ctx->out.value("message", std::string()));

        // Cache it now rather than waiting for a round trip: the operator's next action is usually
        // another test, and it should not ask for the password again just because engined has not
        // answered yet.
        if (!key.empty())
            ctx->api->rememberIssuedKey(ctx->keyOid, key);
    }

    ctx->out["stored"] = pz::util::secret::available();
    sendTestResponse(*ctx->sm, ctx->seqNo, ctx->out);
}

void runKeygen(const std::shared_ptr<ConnectorTest>& ctx)
{
    const TestTarget& target = ctx->target;
    auto req = baseRequest(target);

    // The key-generation path comes from the API Key record; customer estates differ, so it is
    // stated rather than assumed. Credentials are appended as query parameters, the form every
    // PAN-OS release accepts.
    const std::string base = target.keygenEndpoint.empty() ? std::string("/api/?type=keygen") : target.keygenEndpoint;
    req.target = base + (base.find('?') == std::string::npos ? "?" : "&") +
                 "user=" + pz::http::urlEncode(target.username) +
                 "&password=" + pz::http::urlEncode(target.password);

    pz::http::requestAsync(ctx->sm->ioContext(), std::move(req),
                           [ctx](pz::http::ClientResponse res) { onKeygenResponse(ctx, std::move(res)); });
}

void onKeygenResponse(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res)
{
    const bool hadPin = !ctx->target.fingerprint.empty();
    const std::string& host = ctx->target.host;
    json& out = ctx->out;

    if (!recordTlsStep(res, hadPin, out))
    {
        LOG_DEBUG("keygen stopped at TLS (host={}, tls_ok={}, pin_matched={})", host, res.tlsOk, res.pinMatched);
        return afterKey(ctx, std::string());
    }

    if (res.status != 200)
    {
        // The credential exchange the operator most needs to see fail — this is where a wrong
        // password or an unauthorised account surfaces as a device 403.
        LOG_WARN("keygen rejected by device (host={}, status={}, msg={})", host, res.status,
                 xmlErrorMessage(res.body));
        out["steps"]["auth"] =
            stepJson(false, "HTTP " + std::to_string(res.status) + " — " + xmlErrorMessage(res.body));
        out["message"] = xmlErrorMessage(res.body);
        return afterKey(ctx, std::string());
    }

    const std::string key = extractXmlTag(res.body, "key");
    if (key.empty())
    {
        LOG_WARN("keygen returned no key (host={}, msg={})", host, xmlErrorMessage(res.body));
        out["steps"]["auth"] = stepJson(false, xmlErrorMessage(res.body));
        out["message"] = xmlErrorMessage(res.body);
        return afterKey(ctx, std::string());
    }

    // Length only — the key itself never reaches a log line.
    LOG_INFO("keygen succeeded (host={}, key_len={})", host, key.size());
    out["steps"]["auth"] = stepJson(true, "API key issued (" + std::to_string(key.size()) + " chars)");
    afterKey(ctx, key);
}

void runEndpointCall(const std::shared_ptr<ConnectorTest>& ctx, const std::string& key)
{
    const TestTarget& target = ctx->target;
    const json& input = ctx->input;
    json& out = ctx->out;

    auto call = baseRequest(target);

    const std::string apiType = input.value("api_type", std::string("rest"));
    const std::string endpoint = input.value("endpoint", std::string());

    // Path + operator-supplied parameters, percent-encoded here so the operator can type raw
    // values (an XML API cmd=<show><system><info/></system></show> included).
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

    // What the operator is shown as the request line. The key never appears in it — the point
    // of the panel is to debug the path, not to expose the credential.
    std::string displayTarget = path;

    // The two PAN-OS APIs carry the key differently: the XML API takes it as a query parameter
    // (the form every release accepts), the REST API as a header.
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

    // The wire request, with the key redacted (displayTarget hides it) — off by default, on when
    // an operator is chasing why a path answers differently than they expect.
    LOG_TRACE("endpoint request (seq={}, GET {})", ctx->seqNo, displayUrl);

    pz::http::requestAsync(ctx->sm->ioContext(), std::move(call),
                           [ctx](pz::http::ClientResponse res) { onEndpointResponse(ctx, std::move(res)); });
}

void onEndpointResponse(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res)
{
    const std::string apiType = ctx->input.value("api_type", std::string("rest"));
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
    // collect, not merely that it returned 200. Capped because a broad query (every address
    // object on a large firewall) would otherwise be megabytes.
    constexpr std::size_t kMaxBody = 16000;
    const bool truncated = res.body.size() > kMaxBody;
    const bool ok = (res.status == 200);

    // The endpoint outcome, at a level that shows by default: this is what was missing when a
    // stored key returned 403 and the only trace of it lived in the browser's test panel.
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

// Answers mgmtd's request. seqNo is the ticket mgmtd is holding the browser on.
void sendTestResponse(ScandServiceManager& sm, std::uint32_t seqNo, const json& out)
{
    const std::string payload = out.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Scand);
    msg->setDst(pz::ipc::IpcDaemon::Mgmtd);
    msg->setCmd(pz::ipc::IpcCmd::ApiConnectorTestResponse);
    msg->setSeqNo(seqNo);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

// Hands the issued key to engined, the only database writer. The secret is sealed here so the
// plaintext never crosses the IPC socket.
void sendApiKeyState(ScandServiceManager& sm, const std::string& keyOid, const std::string& key, bool ok,
                     const std::string& note)
{
    LOG_DEBUG("persisting api key state to engined (oid={}, ok={}, has_key={})", keyOid, ok, !key.empty());

    json state;
    state["oid"] = keyOid;
    state["ok"] = ok;
    state["note"] = note;

    if (!key.empty())
    {
        if (auto sealed = pz::util::secret::encrypt(key))
            state["secret_enc"] = *sealed;
        else
            LOG_WARN("credential store unavailable — key not persisted (oid={})", keyOid);
    }

    const std::string payload = state.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Scand);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::ApiKeyStateUpdate);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

}

void ApiConnectorTester::run(ApiService& api, ScandServiceManager& sm, std::uint32_t seqNo, const json& input)
{
    auto ctx = std::make_shared<ConnectorTest>();
    ctx->api = &api;
    ctx->sm = &sm;
    ctx->seqNo = seqNo;
    ctx->input = input;

    // Backstop: mgmtd is holding a browser on this seqNo, so a throw must not escape into the event
    // loop and leave the ticket pending forever. Every non-throwing path already answers the ticket
    // exactly once on its own.
    try
    {
        runConnectorTest(ctx);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("connector test failed to start (seq={}, error={})", seqNo, e.what());

        json out;
        out["ok"] = false;
        out["steps"] = json::object();
        out["message"] = std::string("test could not be started: ") + e.what();
        sendTestResponse(sm, seqNo, out);
    }
}

}
