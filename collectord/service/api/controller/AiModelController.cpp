#include "service/api/controller/AiModelController.h"

#include "service/CollectordServiceManager.h"
#include "router/CollectordTxRouter.h"

#include "http/HttpClient.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"
#include "util/Secret.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace pz::collectord
{

namespace
{

using json = nlohmann::json;

// How each vendor is asked, and what its answer looks like. Compiled in for the reason the turn
// endpoints are compiled into pretzel-ai: which host answers for a vendor is a fact about the
// vendor, and a console field for it only buys the chance to point "openai" at something that is
// not OpenAI.
//
// Every one of the three authenticates the list endpoint with the SAME key the turns use. That is
// what makes this operation worth having at all — a list fetched with a different credential would
// not be the list this appliance can actually spend.
struct Vendor
{
    const char* host;
    const char* target;
    const char* authHeader;     // the header the key goes out under
    const char* authPrefix;     // what precedes the key in that header's value, where anything does
    const char* tokenParam;     // see deriveTokenParam below — NOT reported by any of them
};

const Vendor* vendorFor(const std::string& provider)
{
    // max_completion_tokens on OpenAI: the gpt-5 generation renamed the output cap and rejects the
    // old name. The other two still take max_tokens, which is also pretzel-ai's default.
    //
    // This is a DERIVATION, and the only field here that is. None of the three list endpoints
    // reports which name a model's cap goes out under, so it cannot be fetched — and getting it
    // wrong fails the turn rather than degrading it. Per vendor rather than per model because that
    // is the granularity the rename actually happened at; a vendor that splits it per model is a
    // vendor this table can no longer answer for, and the column in ai_provider_model is writable
    // so an operator can correct one without waiting for a release.
    static const Vendor openai{
        "api.openai.com", "/v1/models", "Authorization", "Bearer ", "max_completion_tokens"};
    static const Vendor google{
        "generativelanguage.googleapis.com", "/v1beta/models?pageSize=1000", "x-goog-api-key", "",
        "max_tokens"};
    static const Vendor anthropic{
        "api.anthropic.com", "/v1/models?limit=1000", "x-api-key", "", "max_tokens"};

    if (provider == "openai")
        return &openai;
    if (provider == "google")
        return &google;
    if (provider == "anthropic")
        return &anthropic;
    return nullptr;
}

// OpenAI's list is the whole account: embeddings, speech, transcription, images and moderation sit
// beside the chat models with nothing in the response to tell them apart — no capability field, no
// type discriminator, just ids. So the split has to be made from the name, and this is a HEURISTIC
// rather than a fact the vendor stated.
//
// Conservative in the direction that matters: a chat model wrongly dropped is a model an operator
// cannot select and will report; a non-chat model wrongly kept is one they can select and that
// fails every turn with a vendor error nobody connects back to this list. The other two vendors
// need none of this — Gemini says which generation methods a model supports, and every model
// Anthropic lists serves messages.
bool looksLikeOpenAiChatModel(const std::string& id)
{
    if (id.rfind("gpt", 0) != 0)
        return false;

    for (const auto* marker : {"embedding", "tts", "whisper", "audio", "realtime", "transcribe",
                               "image", "moderation", "search", "instruct", "vision-preview"})
    {
        if (id.find(marker) != std::string::npos)
            return false;
    }
    return true;
}

// { data: [ { id, ... } ] }. No display name is given, so the id stands as the label.
void parseOpenAi(const json& body, const Vendor& v, json& models)
{
    if (!body.contains("data") || !body["data"].is_array())
        return;

    for (const auto& m : body["data"])
    {
        const std::string id = m.value("id", std::string());
        if (id.empty() || !looksLikeOpenAiChatModel(id))
            continue;
        models.push_back({{"id", id}, {"label", id}, {"token_param", v.tokenParam}});
    }
}

// { models: [ { name: "models/gemini-...", displayName, supportedGenerationMethods: [...] } ] }.
// The name carries a "models/" prefix the API expects back on a call but pretzel-ai does not send —
// it qualifies with the provider slug instead — so it is stripped here.
void parseGoogle(const json& body, const Vendor& v, json& models)
{
    if (!body.contains("models") || !body["models"].is_array())
        return;

    for (const auto& m : body["models"])
    {
        std::string name = m.value("name", std::string());
        if (const auto slash = name.rfind('/'); slash != std::string::npos)
            name = name.substr(slash + 1);
        if (name.empty())
            continue;

        // The vendor says which methods a model serves, so no guessing is needed: a model that
        // cannot generateContent cannot serve a turn.
        bool serves = false;
        if (m.contains("supportedGenerationMethods") && m["supportedGenerationMethods"].is_array())
        {
            for (const auto& method : m["supportedGenerationMethods"])
            {
                if (method.is_string() && method.get<std::string>() == "generateContent")
                {
                    serves = true;
                    break;
                }
            }
        }
        if (!serves)
            continue;

        const std::string label = m.value("displayName", name);
        models.push_back({{"id", name}, {"label", label}, {"token_param", v.tokenParam}});
    }
}

// { data: [ { id, display_name, type: "model" } ], has_more, last_id }. Every entry serves messages,
// so there is nothing to filter.
void parseAnthropic(const json& body, const Vendor& v, json& models)
{
    if (!body.contains("data") || !body["data"].is_array())
        return;

    for (const auto& m : body["data"])
    {
        const std::string id = m.value("id", std::string());
        if (id.empty())
            continue;
        models.push_back({{"id", id}, {"label", m.value("display_name", id)}, {"token_param", v.tokenParam}});
    }

    // The page size asked for is the endpoint's maximum. If a vendor ever serves more than that in
    // one account, the list is short and silently so — said out loud rather than paged, because a
    // second request is a second failure mode on a path an operator is watching, and the day this
    // fires is the day to add it.
    if (body.value("has_more", false))
        LOG_WARN("anthropic model list is truncated — the account serves more than one page");
}

// collectord → mgmtd, answering the ticket the browser is holding.
void sendUpdateResponse(CollectordServiceManager& sm, std::uint32_t seqNo, const json& out)
{
    if (seqNo == 0)
        return;

    const std::string payload = out.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Collectord);
    msg->setDst(pz::ipc::IpcDaemon::Mgmtd);
    msg->setCmd(pz::ipc::IpcCmd::AiModelUpdateResponse);
    msg->setSeqNo(seqNo);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Response));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

void fail(CollectordServiceManager& sm, std::uint32_t seqNo, const std::string& provider,
          const std::string& message)
{
    LOG_WARN("ai model refresh failed (seq={}, provider={}, reason={})", seqNo, provider, message);
    sendUpdateResponse(sm, seqNo, json{{"ok", false}, {"provider", provider}, {"message", message}});
}

// collectord → engined: replace this vendor's rows. Sent only on a fetch that produced a list —
// a vendor that could not be reached keeps the models it had, because an empty menu and an
// unreachable vendor are not the same thing and only one of them should empty the picker.
void sendModelUpdate(CollectordServiceManager& sm, const std::string& provider, const json& models)
{
    json out;
    out["provider"] = provider;
    out["models"] = models;

    const std::string payload = out.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Collectord);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::AiModelUpdate);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

// collectord → engined: the sealed key for one vendor, please. Carries the refresh's seqNo so the
// answer can be matched back to the ticket that is waiting on it.
void requestSealedKey(CollectordServiceManager& sm, std::uint32_t seqNo, const std::string& provider)
{
    const std::string payload = json{{"id", provider}}.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Collectord);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::AiCredentialStateRequest);
    msg->setSeqNo(seqNo);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

}

void AiModelController::refresh(CollectordServiceManager& sm, std::uint32_t seqNo, const json& input)
{
    const std::string provider = input.value("provider", std::string());

    if (vendorFor(provider) == nullptr)
        return fail(sm, seqNo, provider, "unknown provider — one of openai, google, anthropic");

    // A second press while the first is still out would overwrite the entry and orphan the earlier
    // ticket. Refused rather than replaced: the operator's first press is the one that is already
    // waiting on a vendor.
    if (!m_pending.emplace(seqNo, provider).second)
        return fail(sm, seqNo, provider, "a refresh is already running on this ticket");

    LOG_DEBUG("ai model refresh started (seq={}, provider={}) — asking engined for the key", seqNo, provider);
    requestSealedKey(sm, seqNo, provider);
}

void AiModelController::receiveKey(CollectordServiceManager& sm, std::uint32_t seqNo, const json& input)
{
    const auto waiting = m_pending.find(seqNo);
    if (waiting == m_pending.end())
    {
        LOG_DEBUG("ai credential state answered no pending refresh (seq={})", seqNo);
        return;
    }

    const std::string provider = waiting->second;
    m_pending.erase(waiting);

    const Vendor* v = vendorFor(provider);
    if (v == nullptr)
        return fail(sm, seqNo, provider, "unknown provider");

    const std::string sealed = input.value("key_enc", std::string());
    if (sealed.empty())
        return fail(sm, seqNo, provider, "no API key is stored for this vendor");

    // A blob that will not open is a real condition, not a parse error: credentials.key was replaced
    // after the key was sealed, and the operator has to paste it again.
    auto key = pz::util::secret::decrypt(sealed);
    if (!key || key->empty())
        return fail(sm, seqNo, provider, "the stored API key could not be opened — re-enter it");

    pz::http::ClientRequest req;
    req.host = v->host;
    req.port = 443;
    req.method = "GET";
    req.target = v->target;
    // A public vendor, not a managed device: verify the chain and the hostname. The fingerprint pin
    // this client was built for answers the opposite problem — a device whose certificate no CA
    // signed — and pinning a vendor would break on their next rotation.
    req.verifyCa = true;
    req.headers.emplace_back(v->authHeader, std::string(v->authPrefix) + *key);
    req.headers.emplace_back("Accept", "application/json");
    if (provider == "anthropic")
        req.headers.emplace_back("anthropic-version", "2023-06-01");
    // Longer than a device call: these are internet round trips over TLS, and the operator is
    // watching a spinner rather than a collection schedule.
    req.timeout = std::chrono::seconds(20);

    LOG_DEBUG("ai model fetch (seq={}, provider={}, host={})", seqNo, provider, v->host);

    pz::http::requestAsync(sm.ioContext(), std::move(req),
                           [smp = &sm, seqNo, provider, v](pz::http::ClientResponse res) {
        if (!res.error.empty())
            return fail(*smp, seqNo, provider, "could not reach the vendor: " + res.error);

        if (res.status != 200)
            return fail(*smp, seqNo, provider,
                        "the vendor answered " + std::to_string(res.status) +
                        (res.status == 401 || res.status == 403 ? " — check the stored API key" : ""));

        json body;
        try
        {
            body = json::parse(res.body);
        }
        catch (const std::exception& e)
        {
            return fail(*smp, seqNo, provider, std::string("the vendor's answer was not JSON: ") + e.what());
        }

        json models = json::array();
        if (provider == "openai")
            parseOpenAi(body, *v, models);
        else if (provider == "google")
            parseGoogle(body, *v, models);
        else
            parseAnthropic(body, *v, models);

        // An answer that parsed but named nothing this appliance can serve. Not written: replacing a
        // working menu with an empty one because a vendor changed their response shape is the failure
        // this whole path exists to avoid.
        if (models.empty())
            return fail(*smp, seqNo, provider, "the vendor listed no usable models — the stored list is unchanged");

        LOG_INFO("ai model refresh ok (seq={}, provider={}, models={})", seqNo, provider, models.size());

        sendModelUpdate(*smp, provider, models);
        sendUpdateResponse(*smp, seqNo, json{{"ok", true},
                                           {"provider", provider},
                                           {"count", models.size()},
                                           {"message", "fetched " + std::to_string(models.size()) + " models"}});
    });
}

}
