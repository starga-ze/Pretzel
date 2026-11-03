#include "service/advisory/AdvisoryService.h"

#include "db/Database.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace pz::engined
{

using json = nlohmann::json;

namespace
{

// Postgres array literal. The elements are vendor product names — free text on their side, which
// has already contained commas and spaces — so every one is quoted and its quotes and backslashes
// escaped. Built here rather than passed as a JSON array and cast, because `text[]` is what the
// GIN index on products is built over and what the containment filter reads.
std::string textArray(const json& values)
{
    std::string out = "{";
    bool first = true;
    for (const auto& v : values)
    {
        if (!v.is_string())
            continue;
        if (!first)
            out += ',';
        first = false;
        out += '"';
        for (const char c : v.get<std::string>())
        {
            if (c == '"' || c == '\\')
                out += '\\';
            out += c;
        }
        out += '"';
    }
    out += '}';
    return out;
}

// A JSON number that has to travel to libpq as text. Absent or non-numeric becomes '', which the
// NULLIF in each statement turns into a real NULL rather than a bogus zero — an advisory with no
// CVSS score is not an advisory scoring 0.0.
std::string numField(const json& root, const char* key)
{
    if (!root.contains(key) || !root[key].is_number())
        return {};
    return std::to_string(root[key].get<double>());
}

std::string strField(const json& root, const char* key)
{
    return root.contains(key) && root[key].is_string() ? root[key].get<std::string>() : std::string();
}

}

void AdvisoryService::handleEvent(EnginedServiceManager& serviceManager, const AdvisoryEvent& event)
{
    (void)serviceManager;

    switch (event.type())
    {
    case AdvisoryEventType::ReceiveUpdate:
        onUpdate(event);
        break;

    default:
        LOG_WARN("unhandled event (type={})", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void AdvisoryService::onUpdate(const AdvisoryEvent& event)
{
    const pz::ipc::IpcMessage* msg = event.message();
    if (!msg || msg->getPayload().empty())
    {
        LOG_WARN("empty AdvisoryUpdate — dropping");
        return;
    }

    const auto& pl = msg->getPayload();
    storeBatch(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
}

void AdvisoryService::storeBatch(const std::string& payloadJson)
{
    json root;
    try
    {
        root = json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse AdvisoryUpdate payload (error={})", e.what());
        return;
    }

    auto& db = pz::db::Database::instance();

    int added = 0, revised = 0, unchanged = 0, rejected = 0;

    for (const auto& a : root.value("advisories", json::array()))
    {
        if (!a.is_object())
            continue;

        const std::string id = strField(a, "id");
        if (id.empty())
        {
            ++rejected;
            continue;
        }

        // The CHECK constraint would reject anything else and take the whole statement with it, so
        // an unexpected severity word is folded here rather than allowed to fail the write. It is
        // logged, because a new band in the feed is a thing to go and look at.
        std::string severity = strField(a, "severity");
        if (severity != "critical" && severity != "high" && severity != "medium" && severity != "low"
            && severity != "none")
        {
            if (!severity.empty())
                LOG_WARN("advisory carries an unknown severity, storing as none (id={}, severity={})", id, severity);
            severity = "none";
        }

        const std::string record = a.contains("record") ? a["record"].dump() : json::object().dump();

        // Returns 't' for a row that did not exist, 'f' for one whose content moved, and nothing at
        // all when the hash matches — so an unchanged advisory costs no write and the three
        // outcomes are counted without a second query. The WHERE on DO UPDATE is what makes the
        // no-op case free: without it every poll would rewrite all 588 rows and bump updated_at.
        const auto result = db.queryScalar(
            "INSERT INTO security_advisory "
            "(id, title, severity, base_score, threat_score, published_at, source_updated, "
            " products, has_fix, record_json, content_sha) "
            "VALUES ($1, $2, $3, NULLIF($4,'')::numeric, NULLIF($5,'')::numeric, "
            " NULLIF($6,'')::timestamptz, NULLIF($7,'')::timestamptz, $8::text[], $9::boolean, "
            " $10::jsonb, $11) "
            "ON CONFLICT (id) DO UPDATE SET "
            " title = EXCLUDED.title, severity = EXCLUDED.severity, "
            " base_score = EXCLUDED.base_score, threat_score = EXCLUDED.threat_score, "
            " published_at = EXCLUDED.published_at, source_updated = EXCLUDED.source_updated, "
            " products = EXCLUDED.products, has_fix = EXCLUDED.has_fix, "
            " record_json = EXCLUDED.record_json, content_sha = EXCLUDED.content_sha, "
            " updated_at = now(), revised_at = now(), notified_at = NULL "
            "WHERE security_advisory.content_sha IS DISTINCT FROM EXCLUDED.content_sha "
            "RETURNING (xmax = 0)",
            {id, strField(a, "title"), severity, numField(a, "base_score"), numField(a, "threat_score"),
             strField(a, "published_at"), strField(a, "source_updated"), textArray(a.value("products", json::array())),
             a.value("has_fix", false) ? "true" : "false", record, strField(a, "content_sha")});

        if (!result)
            ++unchanged;
        else if (*result == "t")
            ++added;
        else
            ++revised;
    }

    LOG_INFO("advisory batch stored (added={}, revised={}, unchanged={}, rejected={})", added, revised, unchanged,
             rejected);

    // The run's own outcome, written once the collector says this was the last batch. A failed run
    // still lands here — the previous advisories stay standing and the console says when the last
    // good sync was, which is the honest answer to "is this list current".
    const json run = root.value("run", json::object());
    if (!run.value("final", false))
        return;

    const std::string status = run.value("status", std::string("ok"));
    const std::string listed = std::to_string(run.value("listed", 0));
    const std::string error = run.value("error", std::string());
    const std::string startedAt = run.value("started_at", std::string());

    // Counted from the table rather than accumulated across batches. A run arrives as several
    // frames — 588 advisories do not fit in one — and this service holds no per-run state on
    // purpose: a restart between batches would otherwise leave a half-counted run on screen, and
    // the table already knows the answer. `revised` is measured from the run's own start instant,
    // which is why collectord stamps it rather than engined.
    db.exec("INSERT INTO security_advisory_sync "
            "(oid, started_at, finished_at, status, listed, stored, revised, error) "
            "VALUES (1, NULLIF($1,'')::timestamptz, now(), $2, $3::int, "
            " (SELECT count(*) FROM security_advisory), "
            " (SELECT count(*) FROM security_advisory "
            "   WHERE $1 <> '' AND revised_at >= $1::timestamptz), $4) "
            "ON CONFLICT (oid) DO UPDATE SET "
            " started_at = EXCLUDED.started_at, finished_at = EXCLUDED.finished_at, "
            " status = EXCLUDED.status, listed = EXCLUDED.listed, stored = EXCLUDED.stored, "
            " revised = EXCLUDED.revised, error = EXCLUDED.error",
            {startedAt, status, listed, error});

    LOG_INFO("advisory sync finished (status={}, listed={})", status, listed);
}

}
