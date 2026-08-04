#include "service/api/ApiUtil.h"

#include "service/CollectordServiceManager.h"
#include "service/api/controller/ConnectorTest.h"

#include "http/UrlEncode.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"
#include "util/Secret.h"

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace pz::collectord
{

using json = nlohmann::json;

// ── Palo Alto cloud ───────────────────────────────────────────────────────────────────────

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

HostPath splitHostPath(const std::string& urlish, const std::string& defaultHost, const std::string& defaultPath)
{
    HostPath hp;
    hp.host = defaultHost;
    hp.path = defaultPath;

    if (urlish.empty())
        return hp;

    std::string rest = urlish;
    if (const auto s = rest.find("://"); s != std::string::npos)
        rest.erase(0, s + 3);

    const auto slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    hp.path = (slash == std::string::npos) ? "/" : rest.substr(slash);

    if (const auto colon = hostport.rfind(':'); colon != std::string::npos)
    {
        try
        {
            hp.port = static_cast<std::uint16_t>(std::stoi(hostport.substr(colon + 1)));
            hostport.erase(colon);
        }
        catch (const std::exception&)
        {
            // Not a port — leave the host as typed and keep the default.
        }
    }

    if (!hostport.empty())
        hp.host = hostport;
    return hp;
}

pz::http::ClientRequest buildOAuthTokenRequest(const HostPath& hp, const std::string& clientId,
                                               const std::string& clientSecret, const std::string& tsgId)
{
    pz::http::ClientRequest req;
    req.host = hp.host;
    req.port = hp.port;
    // A public CA endpoint, not a customer device: verify the chain and the hostname rather than
    // pinning a self-signed certificate the way a device exchange does.
    req.verifyCa = true;
    req.method = "POST";
    req.target = hp.path;
    req.timeout = std::chrono::seconds(15);
    req.headers.push_back({"Authorization", "Basic " + base64(clientId + ":" + clientSecret)});
    req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    req.body = "grant_type=client_credentials&scope=tsg_id:" + pz::http::urlEncode(tsgId);
    return req;
}

// ── Connector-test helpers ────────────────────────────────────────────────────────────────

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
    t.deviceType = body.value("device_type", std::string("ngfw"));
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

pz::http::ClientRequest buildKeygenRequest(const TestTarget& t)
{
    auto req = baseRequest(t);

    // The key-generation path comes from the API Key record; customer estates differ, so it is
    // stated rather than assumed. Credentials are appended as query parameters.
    const std::string base = t.keygenEndpoint.empty() ? std::string("/api/?type=keygen") : t.keygenEndpoint;
    req.target = base + (base.find('?') == std::string::npos ? "?" : "&") + "user=" + pz::http::urlEncode(t.username) +
                 "&password=" + pz::http::urlEncode(t.password);
    return req;
}

std::string readKeygenKey(const pz::http::ClientResponse& res, bool hadPin, nlohmann::json& out)
{
    if (!recordTlsStep(res, hadPin, out))
        return {};

    if (res.status != 200)
    {
        // The credential exchange the operator most needs to see fail — a wrong password or an
        // unauthorised account surfaces as a device 403 here.
        out["steps"]["auth"] = stepJson(false, "HTTP " + std::to_string(res.status) + " — " + xmlErrorMessage(res.body));
        out["message"] = xmlErrorMessage(res.body);
        return {};
    }

    const std::string key = extractXmlTag(res.body, "key");
    if (key.empty())
    {
        out["steps"]["auth"] = stepJson(false, xmlErrorMessage(res.body));
        out["message"] = xmlErrorMessage(res.body);
        return {};
    }

    out["steps"]["auth"] = stepJson(true, "API key issued (" + std::to_string(key.size()) + " chars)");
    return key;
}

void sendTestResponse(CollectordServiceManager& sm, std::uint32_t seqNo, const json& out)
{
    if (seqNo == 0)
        return;

    const std::string payload = out.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Collectord);
    msg->setDst(pz::ipc::IpcDaemon::Mgmtd);
    msg->setCmd(pz::ipc::IpcCmd::ApiConnectorTestResponse);
    msg->setSeqNo(seqNo);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

void sendApiCredentialState(CollectordServiceManager& sm, const std::string& keyOid, const std::string& id,
                            const std::string& pw, const std::string& key, const std::string& expiresAt, bool ok,
                            const std::string& note)
{
    LOG_DEBUG("persisting api key state to engined (oid={}, ok={}, has_key={}, has_cred={}, expires={})", keyOid, ok,
              !key.empty(), !id.empty() || !pw.empty(), expiresAt.empty() ? "none" : expiresAt);

    json state;
    state["oid"] = keyOid;
    state["ok"] = ok;
    state["note"] = note;
    if (!expiresAt.empty())
        state["expires_at"] = expiresAt;

    // The account (id/pw) is the durable credential — for sase the bearer token expires, so the
    // credential is what lets a later session re-issue without re-prompting the operator.
    const auto seal = [&](const char* field, const std::string& plain) {
        if (plain.empty())
            return;
        if (auto sealed = pz::util::secret::encrypt(plain))
            state[field] = *sealed;
        else
            LOG_WARN("credential store unavailable — {} not persisted (oid={})", field, keyOid);
    };
    seal("id_enc", id);
    seal("pw_enc", pw);
    seal("key_enc", key);

    const std::string payload = state.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Collectord);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::ApiCredentialStateUpdate);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

bool sendApiCredentialStore(CollectordServiceManager& sm, const std::string& keyOid, const std::string& id,
                            const std::string& pw)
{
    json state;
    state["oid"] = keyOid;
    // Deliberately no "ok"/"note": engined reads their absence as "no test ran" and keeps the
    // last_test_* columns as they were.

    bool sealedAny = false;
    const auto seal = [&](const char* field, const std::string& plain) {
        if (plain.empty())
            return true;   // nothing to store for this field is not a failure
        if (auto sealed = pz::util::secret::encrypt(plain))
        {
            state[field] = *sealed;
            sealedAny = true;
            return true;
        }
        LOG_WARN("credential store unavailable — {} not persisted (oid={})", field, keyOid);
        return false;
    };

    if (!seal("id_enc", id) || !seal("pw_enc", pw))
        return false;
    if (!sealedAny)
        return false;   // caller asked to store nothing

    const std::string payload = state.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Collectord);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::ApiCredentialStateUpdate);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
    return true;
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

// ── Collection sample ─────────────────────────────────────────────────────────────────────

json buildCollectionSample(const std::string& connectorOid, const std::string& endpointOid,
                           const pz::http::ClientResponse& res, long long latencyMs, std::size_t maxBody)
{
    const bool ok = res.tlsOk && res.requestSent && res.status == 200;

    json sample;
    sample["connector_oid"] = connectorOid;
    sample["endpoint_oid"] = endpointOid;
    sample["ok"] = ok;
    sample["latency_ms"] = latencyMs;
    sample["bytes"] = static_cast<std::int64_t>(res.body.size());

    // Only meaningful when the request left the machine; a pin gate refusal never got a status.
    if (res.requestSent)
        sample["http_status"] = res.status;

    const bool truncated = res.body.size() > maxBody;
    sample["truncated"] = truncated;
    sample["body"] = truncated ? res.body.substr(0, maxBody) : res.body;

    if (!ok)
    {
        std::string error;
        if (!res.tlsOk)
            error = res.error.empty() ? "TLS handshake failed" : res.error;
        else if (!res.requestSent)
            error = res.error.empty() ? "certificate not trusted — pin the device first" : res.error;
        else
            error = "HTTP " + std::to_string(res.status);
        sample["error"] = error;
    }

    return sample;
}

}
