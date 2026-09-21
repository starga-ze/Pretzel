#include "service/chat/ChatService.h"

#include "service/EnginedServiceManager.h"
#include "service/chat/ChatEvent.h"

#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pz::engined
{

namespace
{

// How long a conversation is kept after its last turn. Long enough that "the one from last month"
// is still there, short enough that an appliance holding what employees typed is not holding it
// indefinitely — which is the bargain that makes storing it at all defensible.
constexpr int kRetentionDays = 30;

// Only two things delete a conversation: the person, and this. So it does not need to run often,
// and running it on every turn would be a table scan per message.
constexpr auto kPruneInterval = std::chrono::hours(6);

std::string str(const nlohmann::json& j, const char* key)
{
    return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

}

void ChatService::handleEvent(EnginedServiceManager& serviceManager, const ChatEvent& event)
{
    (void)serviceManager;
    if (event.type() != ChatEventType::ReceiveTurn)
        return;

    const auto* in = event.message();
    if (!in || in->getPayload().empty())
    {
        LOG_WARN("empty ChatTurnStore — dropping");
        return;
    }

    const auto& pl = in->getPayload();
    const std::string payload(reinterpret_cast<const char*>(pl.data()), pl.size());

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(payload);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ChatTurnStore payload ({}) — dropping", e.what());
        return;
    }

    if (root.value("delete", false))
        removeSession(payload);
    else if (root.value("patch", false))
        patchSession(payload);
    else
        storeTurn(payload);

    pruneIfDue();
}

void ChatService::storeTurn(const std::string& payloadJson)
{
    const auto root = nlohmann::json::parse(payloadJson, nullptr, false);
    if (root.is_discarded())
        return;

    const std::string session = str(root, "session");
    const std::string owner = str(root, "owner");
    if (session.empty() || owner.empty())
    {
        LOG_WARN("ChatTurnStore without a session or owner — dropping");
        return;
    }

    auto& db = pz::db::Database::instance();

    // The session first, so the messages have something to reference. Upserted rather than
    // inserted-once: the title and the model are settled on the first turn but the draft and the
    // timestamp move with every one, and a conversation that already exists must not be recreated.
    //
    // `owner` is written only on insert. It is mgmtd's answer to "who is signed in", and an UPDATE
    // that took it from a later message would let a second person's turn re-home someone's
    // conversation onto themselves.
    const bool sessionOk = db.exec(
        "INSERT INTO chat_session (oid, owner, service, title, model, draft, updated_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, now()) "
        "ON CONFLICT (oid) DO UPDATE SET "
        "  title = EXCLUDED.title, model = EXCLUDED.model, draft = EXCLUDED.draft, "
        "  updated_at = now()",
        {session, owner, str(root, "service"), str(root, "title"), str(root, "model"),
         str(root, "draft")});

    if (!sessionOk)
    {
        // Almost always the owner: a turn for an account that has since been removed. Dropped
        // rather than retried — there is nobody for it to belong to.
        LOG_WARN("chat_session write failed (session={}) — the turn is not stored", session);
        return;
    }

    int stored = 0;
    for (const auto& m : root.value("messages", nlohmann::json::array()))
    {
        if (!m.is_object())
            continue;

        const std::string oid = str(m, "oid");
        if (oid.empty())
            continue;

        // A message is written once, and `oid` is what says so. mgmtd files a turn when pretzel-ai
        // answers, and a retry that arrived twice must not double the conversation.
        //
        // The sequence number is assigned HERE, from the rows that exist, rather than taken from
        // the payload. It used to come from the browser - a count of what that tab happened to be
        // holding - and a tab whose view of the conversation was shorter than the stored one sent
        // numbers already taken. With DO NOTHING untargeted those collisions were absorbed as if
        // they were retries, so every turn after 2026-09-02 was dropped and the log below still
        // counted them as stored. The conflict target is `oid` now, which is the only thing that
        // legitimately repeats; a seq cannot collide because nothing outside this statement picks
        // one. Same rule the rest of ChatContext already follows: what this side can establish,
        // this side establishes.
        const auto written = db.queryRows(
            "INSERT INTO chat_message "
            "  (oid, session, seq, role, content, model, ok, code, latency_ms, scan) "
            "VALUES ($1, $2, "
            "        (SELECT COALESCE(MAX(seq), -1) + 1 FROM chat_message WHERE session = $2), "
            "        $3, $4, NULLIF($5,''), "
            "        CASE WHEN $6 = '' THEN NULL ELSE $6::boolean END, "
            "        NULLIF($7,''), CASE WHEN $8 = '' THEN NULL ELSE $8::int END, "
            "        CASE WHEN $9 = '' THEN NULL ELSE $9::jsonb END) "
            "ON CONFLICT (oid) DO NOTHING "
            "RETURNING seq",
            {oid, session, str(m, "role"), str(m, "content"),
             str(m, "model"),
             m.contains("ok") && m["ok"].is_boolean() ? (m["ok"].get<bool>() ? "true" : "false") : "",
             str(m, "code"),
             m.contains("latency_ms") && m["latency_ms"].is_number()
                 ? std::to_string(m["latency_ms"].get<int>()) : "",
             m.contains("scan") && !m["scan"].is_null() ? m["scan"].dump() : ""});

        // RETURNING is what makes the count honest. `exec` reports whether the STATEMENT ran, and
        // an absorbed conflict runs perfectly while writing nothing - which is how a silent drop
        // came to be logged as a store for nineteen days.
        if (!written.empty())
            ++stored;
        else
            LOG_WARN("chat_message not written (session={}, oid={}) - already stored, or the "
                     "insert was refused", session, oid);
    }

    // The content is never logged. It is whatever an employee typed, and this log is read by
    // people who have no business reading it — the same rule ChatController follows on the way in.
    LOG_INFO("chat turn stored (session={}, messages={})", session, stored);
}

void ChatService::removeSession(const std::string& payloadJson)
{
    const auto root = nlohmann::json::parse(payloadJson, nullptr, false);
    if (root.is_discarded())
        return;

    const std::string session = str(root, "session");
    const std::string owner = str(root, "owner");
    if (session.empty() || owner.empty())
    {
        LOG_WARN("chat session delete without a session or owner — dropping");
        return;
    }

    // Matched on the owner as well as the id. mgmtd checks it too, and this is the second lock on
    // the same door: knowing a conversation's id must not be enough to delete someone else's.
    // The messages go with it — chat_message references this ON DELETE CASCADE.
    if (pz::db::Database::instance().exec(
            "DELETE FROM chat_session WHERE oid = $1 AND owner = $2", {session, owner}))
        LOG_INFO("chat session removed (session={})", session);
    else
        LOG_WARN("chat session delete failed (session={})", session);
}

// The fields a conversation carries that are not a turn: its name, and what is typed and not yet
// sent. An UPDATE and not an upsert — a conversation with no turns yet has no row, and that is
// correct: it is a name on a screen until someone says something, and the first turn is what brings
// it into being.
void ChatService::patchSession(const std::string& payloadJson)
{
    const auto root = nlohmann::json::parse(payloadJson, nullptr, false);
    if (root.is_discarded())
        return;

    const std::string session = str(root, "session");
    const std::string owner = str(root, "owner");
    if (session.empty() || owner.empty())
        return;

    // Matched on the owner as well as the id, same as the delete: knowing an id must not be enough
    // to rename someone else's conversation.
    //
    // `updated_at` is deliberately NOT touched. Typing into a conversation is not talking in it,
    // and letting a draft push the retention window out would keep a conversation alive on the
    // strength of an unsent sentence.
    pz::db::Database::instance().exec(
        "UPDATE chat_session SET title = $3, draft = $4 WHERE oid = $1 AND owner = $2",
        {session, owner, str(root, "title"), str(root, "draft")});
}

void ChatService::pruneIfDue()
{
    const auto now = std::chrono::steady_clock::now();
    if (m_lastPrune.time_since_epoch().count() != 0 && now - m_lastPrune < kPruneInterval)
        return;

    m_lastPrune = now;   // set first: a failing sweep must not retry on every subsequent turn
    prune();
}

void ChatService::prune()
{
    // On `updated_at`, not `created_at`: a conversation someone came back to last week is not a
    // month old, whatever day it started on. The messages go with it by cascade.
    pz::db::Database::instance().exec(
        "DELETE FROM chat_session WHERE updated_at < now() - ($1 || ' days')::interval",
        {std::to_string(kRetentionDays)});

    LOG_DEBUG("chat sessions pruned (retention={}d)", kRetentionDays);
}

}
