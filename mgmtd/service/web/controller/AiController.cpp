#include "service/web/controller/AiController.h"

#include "service/MgmtdServiceManager.h"

#include "service/ai/AiConfig.h"

#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

#include "db/Database.h"
#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"
#include "util/Secret.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace pz::mgmtd
{

using json = nlohmann::json;

namespace
{

// The three vendors, same fixed list the commit schema enforces (SettingsController). A key slot
// exists per vendor id rather than per model, because a key is issued by the vendor and works for
// every model they serve — which is also why the id is the vendor's name and not a model family's.
constexpr const char* kAiProviders[] = {"openai", "google", "anthropic"};

// The scan service's key, in the same sealed store under a reserved id.
//
// It is not a vendor that serves turns, and the console shows it on the Guardrail page rather than
// beside the three above — but it is the same kind of thing: a key an operator pastes in, that
// mgmtd seals, that engined writes, and that travels to pretzel-ai on the next push. A second table
// would have duplicated all of that to record which page a row is rendered on.
constexpr const char* kAirsCredentialId = "airs";
// The AI gateway's subscription key. It serves the completion as well as governing inspection when
// that guardrail is chosen, which is why it is configured on the Guardrail page and stored beside
// the scan key rather than among the vendors.
constexpr const char* kGatewayCredentialId = "portkey";

bool knownProvider(const std::string& id)
{
    for (const auto* known : kAiProviders)
    {
        if (id == known)
            return true;
    }
    return false;
}

// Every id the credential endpoints accept, and which store it lives in.
//
// One endpoint pair, two tables. The vendors and the route's own subscriptions hold the same kind of
// secret and are sealed, stored and pushed identically, so a second pair of handlers would have
// differed only in a table name. They are separate tables because a vendor row is one of a set an
// operator adds to and removes from, while each route credential is a single fact about this
// appliance.
const char* credentialScope(const std::string& id)
{
    if (knownProvider(id))
        return "provider";
    if (id == kAirsCredentialId || id == kGatewayCredentialId)
        return "route";
    return nullptr;
}

}

void AiController::credentials(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    (void)req;

    // Every vendor is reported, stored or not: the console renders one card per vendor and needs an
    // answer for each of them, and "no row yet" and "a row with no key" are the same fact to a
    // reader — nothing to serve with.
    json out = json::object();
    for (const auto* id : kAiProviders)
        out[id] = {{"stored", false}};
    // Seeded like the vendors, and for the same reason: the Guardrail page has to render "no key
    // stored" as a state rather than as a missing answer.
    out[kAirsCredentialId] = {{"stored", false}};
    out[kGatewayCredentialId] = {{"stored", false}};

    try
    {
        auto rows = pz::db::Database::instance().queryRows(
            "SELECT id, (key_enc IS NOT NULL)::int, "
            "COALESCE(to_char(updated_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
            "COALESCE(to_char(last_test_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
            "COALESCE(last_test_ok::int::text, ''), COALESCE(last_test_note, '') "
            "FROM ai_provider_credential_state");

        // Both stores in one answer. The two pages that read this each take the ids they render;
        // splitting the response would have meant two round trips for a console that opens both.
        for (auto& r : pz::db::Database::instance().queryRows(
                 "SELECT id, (key_enc IS NOT NULL)::int, "
                 "COALESCE(to_char(updated_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
                 "COALESCE(to_char(last_test_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
                 "COALESCE(last_test_ok::int::text, ''), COALESCE(last_test_note, '') "
                 "FROM ai_route_credential_state"))
            rows.push_back(std::move(r));

        for (const auto& r : rows)
        {
            if (r.size() < 6 || !credentialScope(r[0]))
                continue;
            json e;
            e["stored"] = (r[1] == "1");
            if (!r[2].empty())
                e["updated_at"] = r[2];
            if (!r[3].empty())   // a test has run
                e["last_test"] = {{"at", r[3]}, {"ok", r[4] == "1"}, {"detail", r[5]}};
            out[r[0]] = std::move(e);
        }
    }
    catch (const std::exception& ex)
    {
        LOG_WARN("ai credential state query failed: {}", ex.what());
    }

    // Whether the appliance can seal anything at all. Without the master key a store would fail
    // silently at write time; the console would rather say "not configured" up front.
    json body = {{"providers", std::move(out)}, {"sealing_available", pz::util::secret::available()}};
    fill(resp, 200, body.dump());
}

void AiController::credentialStore(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                   pz::http::HttpResponse& resp)
{
    json input;
    if (!parseBody(req, resp, input))
        return;

    const std::string id = input.value("id", std::string());
    const char* scope = credentialScope(id);
    if (!scope)
        return fill(resp, 400, R"({"error":"id must be one of openai, google, anthropic, airs, portkey"})");

    // engined picks the table from this. Named rather than inferred there: a writer that guessed
    // from the id would put a guardrail key in the vendor table the day a vendor is called "airs".
    json payload = {{"id", id}, {"scope", scope}};
    // Kept for the push below, which cannot read it back out of the store in time. Empty means
    // the key was removed, which is what the push takes a clear to mean.
    std::string apiKey;

    if (input.value("clear", false))
    {
        payload["clear"] = true;
        LOG_INFO("ai credential cleared (id={})", id);
    }
    else
    {
        apiKey = input.value("api_key", std::string());
        if (apiKey.empty())
            return fill(resp, 400, R"({"error":"api_key is required"})");

        if (!pz::util::secret::available())
            return fill(resp, 503, R"({"error":"the appliance has no credentials.key — a key cannot be sealed"})");

        const auto sealed = pz::util::secret::encrypt(apiKey);
        if (!sealed)
            return fill(resp, 500, R"({"error":"the key could not be sealed"})");

        payload["key_enc"] = *sealed;
        // Neither the key nor its length is logged — a length alone narrows a guess.
        LOG_INFO("ai credential sealed, handing to engined (id={})", id);
    }

    const std::string body = payload.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::AiCredentialStateUpdate);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(body.begin(), body.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));

    // The configuration did not change, but what it can do did: an enabled vendor that had no key
    // now has one, or has lost the one it had. pretzel-ai holds the deployment it was last pushed,
    // so without this the key would not reach it until the next settings commit.
    //
    // The key is handed to the push rather than left to be read back: engined is the only database
    // writer and the message above is one-way, so at this instant the table still holds the value
    // being replaced. Reading it here would push the old key and leave the assistant failing until
    // something else triggered a push. If engined's write did fail, the log says so and the next
    // push reconciles from the table.
    pushAiConfig(sm, "vendor key stored", id, apiKey);

    // A Write is fire-and-forget by design — engined answers no one. The console refetches
    // /api/ai/credentials to see the outcome rather than being told one here, which is the honest
    // shape: what it reads back is the stored state, not this handler's optimism.
    fill(resp, 202, json{{"id", id}, {"status", "pending"}}.dump());
}


// ── The model catalog ───────────────────────────────────────────────────────────

// What each vendor last listed, grouped the way the provider editor renders it.
//
// Shaped { openai: { fetched_at, models: [...] }, ... } rather than one flat array: every caller
// asks about one vendor at a time, and a flat list would make each of them group it again. A vendor
// that has never been fetched is absent rather than an empty entry — "not fetched yet" and "this
// account serves nothing" are different states and the console says different things about them.
void AiController::models(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    (void)req;

    json out = json::object();

    try
    {
        // Ordered by id so the picker's rows do not move between fetches. Postgres has no reason to
        // return them in a stable order otherwise, and a list that reshuffles on every refresh is a
        // list an operator cannot scan.
        for (const auto& row : pz::db::Database::instance().queryRows(
                 "SELECT provider, model_id, COALESCE(label, model_id), COALESCE(token_param, ''), "
                 "COALESCE(to_char(fetched_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), '') "
                 "FROM ai_provider_model ORDER BY provider, model_id"))
        {
            if (row.size() < 5 || row[0].empty() || row[1].empty())
                continue;

            const std::string& provider = row[0];
            if (!out.contains(provider))
                out[provider] = json{{"fetched_at", row[4]}, {"models", json::array()}};

            json model{{"id", row[1]}, {"label", row[2]}};
            // Absent rather than empty when unset: the commit payload carries token_param only when
            // there is one, and an empty string there would travel to pretzel-ai as a parameter name.
            if (!row[3].empty())
                model["token_param"] = row[3];

            out[provider]["models"].push_back(std::move(model));
        }
    }
    catch (const std::exception& ex)
    {
        LOG_WARN("ai model catalog query failed: {}", ex.what());
        return fill(resp, 500, R"({"error":"the model catalog could not be read"})");
    }

    fill(resp, 200, out.dump());
}

// Ask collectord to re-fetch one vendor's list.
//
// The vendor is checked here rather than left to collectord: mgmtd is holding a browser on the
// ticket, and a request that names something no daemon can serve should fail where the operator is
// looking, not as a ticket that resolves to an error a hop later.
void AiController::modelsUpdate(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    json input;
    if (!parseBody(req, resp, input))
        return;

    const std::string provider = input.value("id", input.value("provider", std::string()));
    if (!knownProvider(provider))
        return fill(resp, 400, json{{"error", "unknown provider '" + provider + "'"}}.dump());

    const std::uint32_t ticket = sm.nextAiModelTicket();

    const std::string payload = json{{"provider", provider}}.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Collectord);
    msg->setCmd(pz::ipc::IpcCmd::AiModelUpdateRequest);
    msg->setSeqNo(ticket);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));

    LOG_INFO("ai model refresh delegated to collectord (ticket={}, provider={})", ticket, provider);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void AiController::modelsUpdateResult(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                      pz::http::HttpResponse& resp)
{
    const std::string raw = queryParam(req.target, "ticket");
    const auto ticket = static_cast<std::uint32_t>(std::strtoul(raw.c_str(), nullptr, 10));

    if (ticket == 0)
        return fill(resp, 400, R"({"error":"bad ticket"})");

    auto result = sm.takeAiModelResult(ticket);
    if (!result)
        return fill(resp, 200, R"({"status":"pending"})");

    json body = json::parse(*result, nullptr, false);
    if (body.is_discarded())
        return fill(resp, 500, R"({"status":"done","ok":false,"message":"malformed refresh result"})");

    body["status"] = "done";
    fill(resp, 200, body.dump());
}

void AiController::onModelsUpdateResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg)
{
    const auto& pl = msg.getPayload();
    if (pl.empty())
    {
        // The browser is holding this ticket. Dropping the message would leave it polling until its
        // own timeout, so the ticket is answered with the failure instead — same as the tests.
        LOG_WARN("empty ai model refresh response (seq={}) — answering the ticket as failed", msg.getSeqNo());
        sm.setAiModelResult(msg.getSeqNo(), R"({"ok":false,"message":"the daemon returned an empty result"})");
        return;
    }

    sm.setAiModelResult(msg.getSeqNo(), std::string(pl.begin(), pl.end()));
}

}
