#include "service/api/ApiService.h"

#include "service/ScandServiceManager.h"
#include "service/api/ApiEvent.h"

#include "config/Config.h"
#include "http/HttpClient.h"
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
        connector.apiType = (c.value("api_type", std::string("rest")) == "xml") ? ApiType::Xml : ApiType::Rest;
        connector.endpoint = c.value("endpoint", std::string());

        const auto params = c.find("params");
        if (params != c.end() && params->is_array())
        {
            for (const auto& p : *params)
            {
                if (!p.is_object())
                    continue;
                ApiParam param;
                param.name = p.value("name", std::string());
                param.value = p.value("value", std::string());
                if (!param.name.empty())
                    connector.params.push_back(std::move(param));
            }
        }

        connector.pollIntervalSec = c.value("poll_interval_sec", std::int64_t{60});
        connector.enabled = c.value("enabled", true);

        if (connector.oid.empty() || connector.objectOid.empty() || connector.authProfileOid.empty())
        {
            LOG_WARN("skipping connector without oid/object/auth_profile (name={})", connector.name);
            continue;
        }

        if (connector.endpoint.empty() || connector.endpoint.front() != '/')
        {
            LOG_WARN("skipping connector with invalid endpoint (name={}, endpoint={})", connector.name,
                     connector.endpoint);
            continue;
        }

        if (!findProfile(connector.authProfileOid))
        {
            LOG_WARN("connector references unknown auth profile (name={}, auth_profile={})", connector.name,
                     connector.authProfileOid);
        }

        m_connectors.push_back(std::move(connector));
    }
}

void ApiService::start()
{
    m_profiles.clear();
    m_connectors.clear();

    const auto& cfg = apiConfig();
    loadProfiles(cfg);
    loadConnectors(cfg);

    LOG_INFO("api service started (auth_profiles={}, connectors={})", m_profiles.size(), m_connectors.size());
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

const std::vector<ApiConnector>& ApiService::connectors() const
{
    return m_connectors;
}

// ── Connector test ──────────────────────────────────────────────────────────────────────

void ApiService::handleEvent(ScandServiceManager& serviceManager, const ApiEvent& event)
{
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

    // Validation failures answer immediately — mgmtd is holding a browser on this ticket, so
    // every path has to produce exactly one response.
    auto reject = [&](const std::string& message)
    {
        json out;
        out["ok"] = false;
        out["steps"] = json::object();
        out["message"] = message;
        sendTestResponse(serviceManager, seqNo, out);
    };

    if (target.host.empty())
        return reject("target is required");
    if (target.username.empty() || target.password.empty())
        return reject("the auth profile has no username/password entered");

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

    runKeygen(serviceManager, target,
              [this, &serviceManager, seqNo, target, mode, keyOid, input, out](const std::string& key, json&)
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
                  }

                  (*out)["stored"] = pz::util::secret::available();
                  sendTestResponse(serviceManager, seqNo, *out);
              },
              out);
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

    pz::http::requestAsync(serviceManager.ioContext(), std::move(req),
                           [hadPin, out, onDone = std::move(onDone)](pz::http::ClientResponse res)
                           {
                               if (!recordTlsStep(res, hadPin, *out))
                                   return onDone(std::string(), *out);

                               if (res.status != 200)
                               {
                                   (*out)["steps"]["auth"] = stepJson(
                                       false, "HTTP " + std::to_string(res.status) + " — " + xmlErrorMessage(res.body));
                                   (*out)["message"] = xmlErrorMessage(res.body);
                                   return onDone(std::string(), *out);
                               }

                               const std::string key = extractXmlTag(res.body, "key");
                               if (key.empty())
                               {
                                   (*out)["steps"]["auth"] = stepJson(false, xmlErrorMessage(res.body));
                                   (*out)["message"] = xmlErrorMessage(res.body);
                                   return onDone(std::string(), *out);
                               }

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
    (*out)["request"] = {{"method", "GET"},
                         {"url", "https://" + target.host + portPart + displayTarget},
                         {"key_delivery", apiType == "xml" ? "key= query parameter" : "X-PAN-KEY header"}};

    pz::http::requestAsync(
        serviceManager.ioContext(), std::move(call),
        [this, &serviceManager, seqNo, out, apiType](pz::http::ClientResponse res)
        {
            if (!res.tlsOk || !res.requestSent)
            {
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

}
