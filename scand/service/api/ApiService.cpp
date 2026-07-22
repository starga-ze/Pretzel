#include "service/api/ApiService.h"

#include "service/ScandServiceManager.h"
#include "service/api/ApiEvent.h"

#include "config/Config.h"
#include "client/HttpClient.h"
#include "http/UrlEncode.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"
#include "util/Secret.h"

#include <nlohmann/json.hpp>

namespace pz::scand
{

using json = nlohmann::json;

namespace
{

const json& apiConfig()
{
    return pz::config::Config::serviceSection("scand", "api");
}

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

}

void ApiService::loadProfiles(const nlohmann::json& cfg)
{
    const auto it = cfg.find("api_keys");
    if (it == cfg.end() || !it->is_array())
        return;

    for (const auto& p : *it)
    {
        if (!p.is_object())
            continue;

        AuthProfile profile;
        // `uuid`/`id` = legacy keys, from before the single-identity merge.
        profile.oid = p.value("oid", p.value("uuid", p.value("id", std::string())));
        profile.name = p.value("name", std::string());
        profile.vendor = p.value("vendor", std::string());
        profile.username = p.value("username", std::string());
        profile.tls = p.value("tls", std::string("pin"));
        profile.fingerprint = p.value("fingerprint", std::string());

        if (profile.oid.empty())
        {
            LOG_WARN("skipping auth profile without oid (name={})", profile.name);
            continue;
        }

        m_profiles.push_back(std::move(profile));
    }
}

void ApiService::loadEndpoints(const nlohmann::json& cfg)
{
    const auto it = cfg.find("endpoints");
    if (it == cfg.end() || !it->is_array())
        return;

    for (const auto& e : *it)
    {
        if (!e.is_object())
            continue;

        ApiEndpoint endpoint;
        endpoint.oid = e.value("oid", e.value("uuid", std::string()));   // `uuid` = legacy key
        endpoint.name = e.value("name", std::string());
        endpoint.path = e.value("path", std::string());

        // The REST path is the one that carries a version, so it is also the one that says
        // which API this is. Stated explicitly when present, derived otherwise.
        const std::string apiType =
            e.value("api_type", endpoint.path.rfind("/restapi/", 0) == 0 ? std::string("rest") : std::string("xml"));
        endpoint.apiType = (apiType == "xml") ? ApiType::Xml : ApiType::Rest;

        const auto params = e.find("params");
        if (params != e.end() && params->is_array())
        {
            for (const auto& p : *params)
            {
                if (!p.is_object())
                    continue;
                ApiParam param;
                param.name = p.value("name", std::string());
                param.value = p.value("value", std::string());
                if (!param.name.empty())
                    endpoint.params.push_back(std::move(param));
            }
        }

        if (endpoint.oid.empty())
        {
            LOG_WARN("skipping endpoint without oid (name={})", endpoint.name);
            continue;
        }

        if (endpoint.path.empty() || endpoint.path.front() != '/')
        {
            LOG_WARN("skipping endpoint with invalid path (name={}, path={})", endpoint.name, endpoint.path);
            continue;
        }

        m_endpoints.push_back(std::move(endpoint));
    }
}

void ApiService::loadConnectors(const nlohmann::json& cfg)
{
    const auto it = cfg.find("connectors");
    if (it == cfg.end() || !it->is_array())
        return;

    for (const auto& c : *it)
    {
        if (!c.is_object())
            continue;

        ApiConnector connector;
        connector.oid = c.value("oid", c.value("uuid", std::string()));   // `uuid` = legacy key
        connector.name = c.value("name", std::string());
        connector.objectOid = c.value("object", std::string());
        connector.authProfileOid = c.value("auth_profile", std::string());
        // Each item is one endpoint on its own schedule. `endpoint`/`params`/`poll_interval_sec`
        // at the connector level is the pre-policy shape, when a connector meant a single call;
        // such a connector is refused rather than half-read, so a stalled collection is visible.
        if (c.contains("endpoint") || c.contains("params"))
        {
            LOG_WARN("connector still has the single-endpoint shape — rebuild it as a list of "
                     "collected endpoints (name={})",
                     connector.name);
            continue;
        }

        const auto items = c.find("items");
        if (items != c.end() && items->is_array())
        {
            for (const auto& i : *items)
            {
                if (!i.is_object())
                    continue;

                ApiCollectionItem item;
                item.endpointOid = i.value("endpoint", std::string());
                item.pollIntervalSec = i.value("poll_interval_sec", std::int64_t{60});
                item.enabled = i.value("enabled", true);

                if (item.endpointOid.empty())
                {
                    LOG_WARN("skipping collection item without an endpoint (connector={})", connector.name);
                    continue;
                }

                if (item.pollIntervalSec < 1)
                {
                    LOG_WARN("collection item has a non-positive interval, using 60s "
                             "(connector={}, endpoint={})",
                             connector.name, item.endpointOid);
                    item.pollIntervalSec = 60;
                }

                connector.items.push_back(std::move(item));
            }
        }

        if (connector.oid.empty() || connector.objectOid.empty() || connector.authProfileOid.empty())
        {
            LOG_WARN("skipping connector without oid/object/auth_profile (name={})", connector.name);
            continue;
        }

        if (!findProfile(connector.authProfileOid))
        {
            LOG_WARN("connector references unknown auth profile (name={}, auth_profile={})", connector.name,
                     connector.authProfileOid);
        }

        if (connector.items.empty())
        {
            LOG_WARN("connector collects nothing — no endpoints listed (name={})", connector.name);
        }

        // Warned about, not dropped: mgmtd refuses to commit a dangling reference, so reaching
        // this means the config was edited outside the UI. Keeping the connector visible beats
        // making it disappear; the collector skips the item it cannot resolve.
        for (const auto& item : connector.items)
        {
            if (!findEndpoint(item.endpointOid))
            {
                LOG_WARN("connector references unknown endpoint (name={}, endpoint={})", connector.name,
                         item.endpointOid);
            }
        }

        m_connectors.push_back(std::move(connector));
    }
}

void ApiService::start()
{
    m_profiles.clear();
    m_endpoints.clear();
    m_connectors.clear();

    const auto& cfg = apiConfig();

    // Order matters: connectors resolve their endpoint and auth-profile references as they load.
    loadProfiles(cfg);
    loadEndpoints(cfg);
    loadConnectors(cfg);

    LOG_INFO("api service started (auth_profiles={}, endpoints={}, connectors={})", m_profiles.size(),
             m_endpoints.size(), m_connectors.size());
}

const std::vector<AuthProfile>& ApiService::profiles() const
{
    return m_profiles;
}

const AuthProfile* ApiService::findProfile(const std::string& oid) const
{
    for (const auto& p : m_profiles)
    {
        if (p.oid == oid)
            return &p;
    }
    return nullptr;
}

const std::vector<ApiEndpoint>& ApiService::endpoints() const
{
    return m_endpoints;
}

const ApiEndpoint* ApiService::findEndpoint(const std::string& oid) const
{
    for (const auto& e : m_endpoints)
    {
        if (e.oid == oid)
            return &e;
    }
    return nullptr;
}

const std::vector<ApiConnector>& ApiService::connectors() const
{
    return m_connectors;
}

// ── Connector test ──────────────────────────────────────────────────────────────────────

void ApiService::handleEvent(ScandServiceManager& serviceManager, const ApiEvent& event)
{
    const pz::ipc::IpcMessage* msg = event.message();

    if (event.type() == ApiEventType::ReceiveKeyState)
    {
        if (!msg || msg->getPayload().empty())
        {
            LOG_WARN("empty ApiKeyStateResponse — dropping");
            return;
        }
        const auto& body = msg->getPayload();
        try
        {
            cacheKeys(json::parse(std::string(reinterpret_cast<const char*>(body.data()), body.size())));
        }
        catch (const std::exception& e)
        {
            LOG_WARN("failed to parse ApiKeyStateResponse (error={})", e.what());
        }
        return;
    }

    if (event.type() != ApiEventType::ReceiveConnectorTestRequest)
    {
        return;
    }

    const pz::ipc::IpcMessage* in = event.message();
    if (!in || in->getPayload().empty())
    {
        LOG_WARN("empty ApiConnectorTestRequest — dropping");
        return;
    }

    const auto& pl = in->getPayload();
    json input;
    try
    {
        input = json::parse(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ApiConnectorTestRequest payload (error={})", e.what());
        return;
    }

    // Backstop: mgmtd is holding a browser on this seqNo, so a throw must not escape into the
    // event loop and leave the ticket pending forever.
    try
    {
        runConnectorTest(serviceManager, in->getSeqNo(), input);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("connector test failed to start (seq={}, error={})", in->getSeqNo(), e.what());

        json out;
        out["ok"] = false;
        out["steps"] = json::object();
        out["message"] = std::string("test could not be started: ") + e.what();
        sendTestResponse(serviceManager, in->getSeqNo(), out);
    }
}

void ApiService::runConnectorTest(ScandServiceManager& serviceManager, std::uint32_t seqNo, const json& input)
{
    const TestTarget target = parseTestTarget(input);
    const std::string mode = input.value("mode", std::string("keygen"));

    LOG_DEBUG("connector test received (seq={}, mode={}, host={}, port={}, api_key_oid={})", seqNo, mode,
              target.host, target.port, target.authProfileOid);

    // Validation failures answer immediately — mgmtd is holding a browser on this ticket, so
    // every path has to produce exactly one response.
    auto reject = [&](const std::string& message)
    {
        LOG_DEBUG("connector test rejected (seq={}, reason={})", seqNo, message);
        json out;
        out["ok"] = false;
        out["steps"] = json::object();
        out["message"] = message;
        sendTestResponse(serviceManager, seqNo, out);
    };

    if (target.host.empty())
        return reject("target is required");

    // A key already issued for this profile stands in for the password: it was sealed at
    // issuance and lives in api_key_state, so the operator types the password once rather than
    // once per browser session. Falling back to keygen keeps the first run — and a re-issue
    // after the key is rejected — working.
    const std::string stored = issuedKey(target.authProfileOid);

    if (stored.empty() && (target.username.empty() || target.password.empty()))
    {
        return reject("no key has been issued for this API key yet, and its password is not "
                      "available — enter the password on the API Key page and run the key "
                      "generation once");
    }

    if (mode == "endpoint")
    {
        const std::string endpoint = input.value("endpoint", std::string());
        const std::string apiType = input.value("api_type", std::string("rest"));
        if (endpoint.empty() || endpoint.front() != '/')
            return reject("endpoint must be a path starting with /");
        if (apiType != "xml" && apiType != "rest")
            return reject("api_type must be xml or rest");
        if (!input.value("params", json::array()).is_array())
            return reject("params must be an array of {name, value}");
    }

    auto out = std::make_shared<json>();
    (*out)["steps"] = json::object();

    const std::string keyOid = input.value("oid", std::string());

    auto proceed = [this, &serviceManager, seqNo, target, mode, keyOid, input, out](const std::string& key, json&)
              {
                  if (mode == "endpoint")
                  {
                      if (key.empty())
                      {
                          (*out)["ok"] = false;
                          return sendTestResponse(serviceManager, seqNo, *out);
                      }
                      return runEndpointCall(serviceManager, target, input, key, seqNo, out);
                  }

                  // Keygen test: the issued key is persisted, and only the outcome — not the
                  // key — goes back to the browser.
                  (*out)["ok"] = !key.empty();
                  if (!key.empty())
                      (*out)["message"] = "API key generated";

                  if (!keyOid.empty())
                  {
                      sendApiKeyState(serviceManager, keyOid, key, !key.empty(),
                                      out->value("message", std::string()));

                      // Cache it now rather than waiting for a round trip: the operator's next
                      // action is usually another test, and it should not ask for the password
                      // again just because engined has not answered yet.
                      if (!key.empty())
                          m_issuedKeys[keyOid] = key;
                  }

                  (*out)["stored"] = pz::util::secret::available();
                  sendTestResponse(serviceManager, seqNo, *out);
    };

    if (!stored.empty())
    {
        LOG_DEBUG("connector test using stored key (seq={}, api_key_oid={})", seqNo, target.authProfileOid);
        (*out)["steps"]["auth"] = stepJson(true, "using the key already issued for this profile");
        (*out)["used_stored_key"] = true;
        return proceed(stored, *out);
    }

    LOG_DEBUG("connector test has no stored key — running keygen (seq={}, host={})", seqNo, target.host);
    runKeygen(serviceManager, target, std::move(proceed), out);
}

void ApiService::runKeygen(ScandServiceManager& serviceManager, const TestTarget& target,
                           std::function<void(const std::string&, json&)> onDone, std::shared_ptr<json> out)
{
    auto req = baseRequest(target);

    // The key-generation path comes from the API Key record; customer estates differ, so it is
    // stated rather than assumed. Credentials are appended as query parameters, the form every
    // PAN-OS release accepts.
    const std::string base = target.keygenEndpoint.empty() ? std::string("/api/?type=keygen") : target.keygenEndpoint;
    req.target = base + (base.find('?') == std::string::npos ? "?" : "&") +
                 "user=" + pz::http::urlEncode(target.username) +
                 "&password=" + pz::http::urlEncode(target.password);

    const bool hadPin = !target.fingerprint.empty();
    const std::string host = target.host;

    pz::http::requestAsync(serviceManager.ioContext(), std::move(req),
                           [hadPin, host, out, onDone = std::move(onDone)](pz::http::ClientResponse res)
                           {
                               if (!recordTlsStep(res, hadPin, *out))
                               {
                                   LOG_DEBUG("keygen stopped at TLS (host={}, tls_ok={}, pin_matched={})", host,
                                             res.tlsOk, res.pinMatched);
                                   return onDone(std::string(), *out);
                               }

                               if (res.status != 200)
                               {
                                   // The credential exchange the operator most needs to see fail — this is where
                                   // a wrong password or an unauthorised account surfaces as a device 403.
                                   LOG_WARN("keygen rejected by device (host={}, status={}, msg={})", host,
                                            res.status, xmlErrorMessage(res.body));
                                   (*out)["steps"]["auth"] = stepJson(
                                       false, "HTTP " + std::to_string(res.status) + " — " + xmlErrorMessage(res.body));
                                   (*out)["message"] = xmlErrorMessage(res.body);
                                   return onDone(std::string(), *out);
                               }

                               const std::string key = extractXmlTag(res.body, "key");
                               if (key.empty())
                               {
                                   LOG_WARN("keygen returned no key (host={}, msg={})", host, xmlErrorMessage(res.body));
                                   (*out)["steps"]["auth"] = stepJson(false, xmlErrorMessage(res.body));
                                   (*out)["message"] = xmlErrorMessage(res.body);
                                   return onDone(std::string(), *out);
                               }

                               // Length only — the key itself never reaches a log line.
                               LOG_INFO("keygen succeeded (host={}, key_len={})", host, key.size());
                               (*out)["steps"]["auth"] =
                                   stepJson(true, "API key issued (" + std::to_string(key.size()) + " chars)");
                               onDone(key, *out);
                           });
}

void ApiService::runEndpointCall(ScandServiceManager& serviceManager, const TestTarget& target, const json& input,
                                 const std::string& key, std::uint32_t seqNo, std::shared_ptr<json> out)
{
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
    (*out)["request"] = {{"method", "GET"},
                         {"url", displayUrl},
                         {"key_delivery", apiType == "xml" ? "key= query parameter" : "X-PAN-KEY header"}};

    // The wire request, with the key redacted (displayTarget hides it) — off by default, on when
    // an operator is chasing why a path answers differently than they expect.
    LOG_TRACE("endpoint request (seq={}, GET {})", seqNo, displayUrl);

    pz::http::requestAsync(
        serviceManager.ioContext(), std::move(call),
        [this, &serviceManager, seqNo, out, apiType](pz::http::ClientResponse res)
        {
            if (!res.tlsOk || !res.requestSent)
            {
                LOG_WARN("endpoint test could not send (seq={}, error={})", seqNo,
                         res.error.empty() ? "request was not sent" : res.error);
                (*out)["steps"]["endpoint"] = stepJson(false, res.error.empty() ? "request was not sent" : res.error);
                (*out)["ok"] = false;
                (*out)["message"] = res.error;
                return sendTestResponse(serviceManager, seqNo, *out);
            }

            // The body is returned so the operator can confirm the path produced what they meant
            // to collect, not merely that it returned 200. Capped because a broad query (every
            // address object on a large firewall) would otherwise be megabytes.
            constexpr std::size_t kMaxBody = 16000;
            const bool truncated = res.body.size() > kMaxBody;
            const bool ok = (res.status == 200);

            // The endpoint outcome, at a level that shows by default: this is what was missing when a
            // stored key returned 403 and the only trace of it lived in the browser's test panel.
            if (ok)
                LOG_INFO("endpoint test result (seq={}, status={}, bytes={})", seqNo, res.status, res.body.size());
            else
                LOG_WARN("endpoint test result (seq={}, status={}, bytes={})", seqNo, res.status, res.body.size());

            (*out)["steps"]["endpoint"] =
                stepJson(ok, "HTTP " + std::to_string(res.status) +
                                 (ok ? "" : " — " + (apiType == "xml" ? xmlErrorMessage(res.body)
                                                                     : res.body.substr(0, 160))));
            (*out)["ok"] = ok;
            (*out)["response"] = {{"status", res.status},
                                  {"body", res.body.substr(0, kMaxBody)},
                                  {"bytes", res.body.size()},
                                  {"truncated", truncated}};
            (*out)["message"] = ok ? "endpoint responded" : "endpoint returned HTTP " + std::to_string(res.status);

            sendTestResponse(serviceManager, seqNo, *out);
        });
}

void ApiService::sendTestResponse(ScandServiceManager& serviceManager, std::uint32_t seqNo, const json& out)
{
    const std::string payload = out.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Scand);
    msg->setDst(pz::ipc::IpcDaemon::Mgmtd);
    msg->setCmd(pz::ipc::IpcCmd::ApiConnectorTestResponse);
    msg->setSeqNo(seqNo);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void ApiService::sendApiKeyState(ScandServiceManager& serviceManager, const std::string& keyOid,
                                 const std::string& key, bool ok, const std::string& note)
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

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}


void ApiService::requestKeys(ScandServiceManager& serviceManager)
{
    LOG_DEBUG("requesting issued api keys from engined");
    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Scand);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::ApiKeyStateRequest);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void ApiService::cacheKeys(const json& payload)
{
    const auto keys = payload.value("keys", json::array());
    if (!keys.is_array())
        return;

    std::unordered_map<std::string, std::string> opened;
    std::size_t failed = 0;

    for (const auto& k : keys)
    {
        if (!k.is_object())
            continue;

        const std::string oid = k.value("oid", std::string());
        const std::string sealed = k.value("secret_enc", std::string());
        if (oid.empty() || sealed.empty())
            continue;

        // A blob that will not open is a real condition, not a parse error: credentials.key was
        // replaced or lost. Say how many rather than which, so no oid/key pairing reaches a log.
        if (auto plain = pz::util::secret::decrypt(sealed))
            opened.emplace(oid, *plain);
        else
            ++failed;
    }

    m_issuedKeys = std::move(opened);

    if (failed)
    {
        LOG_WARN("api keys received (usable={}, unreadable={}) — credentials.key may have changed; "
                 "re-run the key generation for those profiles",
                 m_issuedKeys.size(), failed);
    }
    else
    {
        LOG_INFO("api keys received (usable={})", m_issuedKeys.size());
    }
}

const std::string& ApiService::issuedKey(const std::string& authProfileOid) const
{
    static const std::string kNone;
    const auto it = m_issuedKeys.find(authProfileOid);
    return it == m_issuedKeys.end() ? kNone : it->second;
}

}
