#include "service/api/controller/CredentialController.h"

#include "service/CollectordServiceManager.h"
#include "service/api/ApiService.h"
#include "service/api/ApiUtil.h"
#include "service/api/controller/ConnectorTest.h"

#include "http/HttpClient.h"
#include "http/UrlEncode.h"
#include "util/Logger.h"
#include "util/Secret.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

namespace pz::collectord
{

namespace
{

using json = nlohmann::json;

// Standard base64 (for the SASE token request's HTTP Basic credential). Small enough to keep local
// rather than widen the Secret module's surface.
std::string base64(const std::string& in)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in)
    {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0)
        {
            out.push_back(T[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6)
        out.push_back(T[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

// The keygen-test outcome: persist the issued credential to engined (the only DB writer) and answer
// the browser. Only the outcome — not the key — goes back. Runs for both NGFW keys and SASE tokens.
void finishKeygen(const std::shared_ptr<ConnectorTest>& ctx, const std::string& key)
{
    ctx->out["ok"] = !key.empty();
    if (!key.empty())
        ctx->out["message"] = "API key generated";

    // Hand the issued key/token back to the operator who just generated it, before it is sealed. It
    // is theirs — the device minted it from their own account — and a PAN-OS key is routinely copied
    // out for other tooling, so hiding it here just means generating it twice.
    //
    // It stays a one-shot: this rides the held test ticket (drained on read, never persisted), the
    // stored copy is sealed, and nothing about it is logged. The headless auto-refresh path uses
    // seqNo 0, where sendTestResponse returns without transmitting anything.
    if (!key.empty())
    {
        ctx->out["key"] = key;
        if (!ctx->expiresAt.empty())
            ctx->out["expires_at"] = ctx->expiresAt;   // SASE tokens are short-lived; say when it dies
    }

    if (!ctx->keyOid.empty())
    {
        sendApiCredentialState(*ctx->sm, ctx->keyOid, ctx->target.username, ctx->target.password, key, ctx->expiresAt,
                               !key.empty(), ctx->out.value("message", std::string()));

        // Cache it now rather than waiting for a round trip: the operator's next action is usually
        // another test, and it should not ask for the password again just because engined has not
        // answered yet.
        if (!key.empty())
            ctx->api->rememberIssuedKey(ctx->keyOid, key);
    }

    ctx->out["stored"] = pz::util::secret::available();
    sendTestResponse(*ctx->sm, ctx->seqNo, ctx->out);
}

void onKeygen(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res)
{
    const std::string key = readKeygenKey(res, !ctx->target.fingerprint.empty(), ctx->out);
    if (key.empty())
        LOG_WARN("keygen produced no key (host={})", ctx->target.host);
    else
        LOG_INFO("keygen succeeded (host={}, key_len={})", ctx->target.host, key.size());   // length only
    finishKeygen(ctx, key);
}

void onSaseResponse(std::shared_ptr<ConnectorTest> ctx, pz::http::ClientResponse res)
{
    if (!res.tlsOk)
    {
        LOG_WARN("SASE token failed — auth server unreachable (seq={}, err={})", ctx->seqNo, res.error);
        ctx->out["steps"]["tls"] =
            stepJson(false, res.error.empty() ? "could not reach the SASE auth server" : res.error);
        ctx->out["message"] = res.error.empty() ? "could not reach the SASE auth server" : res.error;
        return finishKeygen(ctx, "");
    }
    ctx->out["steps"]["tls"] = stepJson(true, "connected to the SASE auth server");

    const auto body = json::parse(res.body, nullptr, false);
    const bool haveJson = !body.is_discarded() && body.is_object();

    if (res.status != 200)
    {
        std::string detail = "auth server returned HTTP " + std::to_string(res.status);
        if (haveJson)
        {
            const std::string e = body.value("error_description", body.value("error", std::string()));
            if (!e.empty())
                detail = e;
        }
        LOG_WARN("SASE token rejected (seq={}, http={}, detail={})", ctx->seqNo, res.status, detail);
        ctx->out["steps"]["auth"] = stepJson(false, detail);
        ctx->out["message"] = detail;
        return finishKeygen(ctx, "");
    }

    const std::string token = haveJson ? body.value("access_token", std::string()) : std::string();
    if (token.empty())
    {
        LOG_WARN("SASE token response had no access_token (seq={})", ctx->seqNo);
        ctx->out["steps"]["auth"] = stepJson(false, "no access_token in the auth server response");
        ctx->out["message"] = "token issuance failed";
        return finishKeygen(ctx, "");
    }

    // Record the token's expiry so the UI can show it (SASE tokens live ~15 min). Prefer the
    // response's expires_in (seconds from now).
    const long expiresIn = haveJson ? body.value("expires_in", 0L) : 0L;
    if (expiresIn > 0)
    {
        const std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() +
                                                                    std::chrono::seconds(expiresIn));
        std::tm tmv{};
        gmtime_r(&tt, &tmv);
        char buf[32];
        if (std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv) > 0)
            ctx->expiresAt = buf;
    }

    LOG_INFO("SASE token issued (seq={}, token_len={}, expires_in={}s)", ctx->seqNo, token.size(), expiresIn);
    ctx->out["steps"]["auth"] = stepJson(true, "access token issued");
    finishKeygen(ctx, token);
}

// SASE key issuance: exchange the service-account client id/secret for a bearer token at Palo Alto's
// OAuth server. The tenant (TSG) id is the device's target. Unlike a device keygen this hits a public
// CA endpoint, so the chain+hostname are verified (verifyCa) rather than a self-signed pin.
void runSaseToken(const std::shared_ptr<ConnectorTest>& ctx)
{
    const TestTarget& t = ctx->target;

    std::string host = "auth.apps.paloaltonetworks.com";
    std::string path = "/oauth2/access_token";
    std::uint16_t port = 443;
    if (!t.keygenEndpoint.empty())
    {
        std::string rest = t.keygenEndpoint;
        if (const auto s = rest.find("://"); s != std::string::npos)
            rest.erase(0, s + 3);
        const auto slash = rest.find('/');
        std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
        path = (slash == std::string::npos) ? "/" : rest.substr(slash);
        if (const auto colon = hostport.rfind(':'); colon != std::string::npos)
        {
            try
            {
                port = static_cast<std::uint16_t>(std::stoi(hostport.substr(colon + 1)));
                hostport.erase(colon);
            }
            catch (const std::exception&)
            {
            }
        }
        if (!hostport.empty())
            host = hostport;
    }

    pz::http::ClientRequest req;
    req.host = host;
    req.port = port;
    req.verifyCa = true;
    req.method = "POST";
    req.target = path;
    req.timeout = std::chrono::seconds(15);
    req.headers.push_back({"Authorization", "Basic " + base64(t.username + ":" + t.password)});
    req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    req.body = "grant_type=client_credentials&scope=tsg_id:" + pz::http::urlEncode(t.host);

    LOG_DEBUG("SASE token request (seq={}, tsg={})", ctx->seqNo, t.host);
    pz::http::requestAsync(ctx->sm->ioContext(), std::move(req),
                           [ctx](pz::http::ClientResponse res) { onSaseResponse(ctx, std::move(res)); });
}

}

void CredentialController::runKeygenTest(ApiService& api, CollectordServiceManager& sm, std::uint32_t seqNo,
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
        ctx->keyOid = input.value("oid", std::string());
        ctx->target = parseTestTarget(input);

        // The browser holds the typed password only in the moment it was entered; from then on it
        // shows bullets and sends none. Fall back to the credential stored for this API Key, so a
        // test runs from any browser rather than only the one that typed it.
        if (ctx->target.password.empty())
        {
            if (const IssuedCredential* cred = api.credentialFor(ctx->keyOid))
            {
                if (ctx->target.username.empty())
                    ctx->target.username = cred->id;
                ctx->target.password = cred->pw;
            }
        }

        const TestTarget& t = ctx->target;

        LOG_DEBUG("keygen test (seq={}, host={}, type={}, api_key_oid={})", seqNo, t.host, t.deviceType,
                  t.authProfileOid);

        if (t.host.empty())
            return rejectTest(ctx, "target is required");

        ctx->out["steps"] = json::object();

        // SASE mints a short-lived OAuth token from a fixed auth server rather than a device key.
        if (t.deviceType == "sase")
        {
            if (t.username.empty() || t.password.empty())
                return rejectTest(ctx, "the SASE client id and client secret are required to issue a token");
            return runSaseToken(ctx);
        }

        // NGFW: reuse the key already issued for this profile if there is one; otherwise issue a
        // fresh one from the account credential.
        const std::string stored = api.issuedKey(t.authProfileOid);
        if (!stored.empty())
        {
            ctx->out["steps"]["auth"] = stepJson(true, "using the key already issued for this profile");
            ctx->out["used_stored_key"] = true;
            return finishKeygen(ctx, stored);
        }

        if (t.username.empty() || t.password.empty())
            return rejectTest(ctx, "no key has been issued for this API key yet, and its password is not "
                                   "available — enter the password on the API Key page and run the key "
                                   "generation once");

        pz::http::requestAsync(sm.ioContext(), buildKeygenRequest(t),
                               [ctx](pz::http::ClientResponse res) { onKeygen(ctx, std::move(res)); });
    }
    catch (const std::exception& e)
    {
        LOG_WARN("keygen test failed to start (seq={}, error={})", seqNo, e.what());
        json out;
        out["ok"] = false;
        out["steps"] = json::object();
        out["message"] = std::string("test could not be started: ") + e.what();
        sendTestResponse(sm, seqNo, out);
    }
}

// Store an API Key's account credential without calling any device. The operator types the password
// once, in one browser; from any other browser (or machine) that plaintext does not exist, so it has
// to live on the appliance. It is sealed here and stored in api_credential_state.id_enc/pw_enc — the
// same columns a passing keygen writes — and never enters running_config. No test outcome is attached,
// so the key's last-test status is left as whatever the last real test made it.
void CredentialController::storeCredential(ApiService& api, CollectordServiceManager& sm, std::uint32_t seqNo,
                                           const json& input)
{
    json out;
    out["steps"] = json::object();

    try
    {
        const std::string oid = input.value("oid", std::string());

        std::string username;
        std::string password;
        // is_object() is checked rather than assumed: value() throws on a non-object.
        if (const auto secrets = input.value("secrets", json::object()); secrets.is_object())
        {
            username = secrets.value("username", std::string());
            password = secrets.value("password", std::string());
        }

        if (oid.empty() || password.empty())
        {
            out["ok"] = false;
            out["message"] = "the API Key and its password are both required";
        }
        else if (!sendApiCredentialStore(sm, oid, username, password))
        {
            LOG_WARN("credential not stored — credential store unavailable (oid={})", oid);
            out["ok"] = false;
            out["message"] = "the credential store is unavailable — the password was not saved";
        }
        else
        {
            // Cache it now so the operator's next action (usually a test) does not have to wait for
            // engined's persist round-trip.
            api.rememberCredential(oid, username, password);
            LOG_INFO("api credential sealed and sent to engined (oid={})", oid);   // never log the secret
            out["ok"] = true;
            out["message"] = "credential saved";
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("credential store failed (seq={}, error={})", seqNo, e.what());
        out["ok"] = false;
        out["message"] = std::string("the credential could not be saved: ") + e.what();
    }

    sendTestResponse(sm, seqNo, out);
}

// TLS-only probe: open a TLS connection to the device and read back its certificate fingerprint,
// sending no credentials and no meaningful request. This is how an NGFW gets pinned when it is first
// created — trust-on-first-use, before any API Key exists — so the later keygen test finds the pin
// already in place. On first contact (no pin passed) recordTlsStep reports the fingerprint and marks
// the step "not yet trusted"; the browser shows it and lets the operator confirm it onto the device.
void CredentialController::runTlsProbe(CollectordServiceManager& sm, std::uint32_t seqNo, const json& input)
{
    auto ctx = std::make_shared<ConnectorTest>();
    ctx->sm = &sm;
    ctx->seqNo = seqNo;
    ctx->input = input;

    // Backstop: mgmtd holds a browser on this seqNo, so a throw must not leave the ticket pending.
    try
    {
        ctx->target = parseTestTarget(input);   // host[:port] + optional already-pinned fingerprint
        const TestTarget& t = ctx->target;
        ctx->out["steps"] = json::object();

        if (t.host.empty())
            return rejectTest(ctx, "target is required");

        LOG_DEBUG("tls probe (seq={}, host={}, port={})", seqNo, t.host, t.port);

        // GET / is enough to force the handshake; the response is irrelevant — an unpinned peer never
        // even receives the request, but the certificate is captured during the handshake either way.
        pz::http::ClientRequest req = baseRequest(t);
        req.method = "GET";
        req.target = "/";

        pz::http::requestAsync(sm.ioContext(), std::move(req), [ctx](pz::http::ClientResponse res) {
            const bool hadPin = !ctx->target.fingerprint.empty();
            const bool trusted = recordTlsStep(res, hadPin, ctx->out);
            // "ok" here means we got a fingerprint to show, not that the request succeeded: a first
            // contact is a successful probe even though its tls step is marked untrusted.
            const bool haveFingerprint = !ctx->out.value("fingerprint", std::string()).empty();
            ctx->out["ok"] = haveFingerprint;

            if (trusted)
                ctx->out["message"] = "certificate already trusted";
            else if (haveFingerprint && !hadPin)
                ctx->out["message"] = "certificate retrieved — confirm the fingerprint to trust it";
            // Otherwise recordTlsStep's own message stands: a handshake failure, or — when a pin was
            // passed and did not match — the mismatch warning, which must not be softened into a
            // routine "confirm this certificate" prompt.

            sendTestResponse(*ctx->sm, ctx->seqNo, ctx->out);
        });
    }
    catch (const std::exception& e)
    {
        LOG_WARN("tls probe failed to start (seq={}, error={})", seqNo, e.what());
        json out;
        out["ok"] = false;
        out["steps"] = json::object();
        out["message"] = std::string("probe could not be started: ") + e.what();
        sendTestResponse(sm, seqNo, out);
    }
}

}
