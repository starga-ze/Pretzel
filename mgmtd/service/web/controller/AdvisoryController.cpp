#include "service/web/controller/AdvisoryController.h"

#include "service/web/WebUtil.h"

#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "service/MgmtdServiceManager.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pz::mgmtd
{

using json = nlohmann::json;

namespace
{

// The one timestamp format the whole web surface emits. Postgres prints a whole-hour zone as `+09`,
// which is not what ISO 8601 asks for — the frontend pads it back before parsing.
constexpr const char* kTs = "YYYY-MM-DD\"T\"HH24:MI:SSOF";

constexpr int kDefaultLimit = 25;
constexpr int kMaxLimit = 100;

// Below this the server is not asked — the page enforces the same floor, and a one-character LIKE
// over the whole catalogue is a scan that returns everything.
constexpr std::size_t kMinQuery = 3;

int intParam(const std::string& target, const char* key, int fallback, int lo, int hi)
{
    const auto s = queryParam(target, key);
    if (s.empty())
        return fallback;
    try
    {
        return std::clamp(std::stoi(s), lo, hi);
    }
    catch (...)
    {
        return fallback;
    }
}

bool isSeverity(const std::string& s)
{
    return s == "critical" || s == "high" || s == "medium" || s == "low" || s == "none";
}

// `{"PAN-OS","Prisma Access"}` back into a JSON array. Postgres prints a text[] this way and
// quotes an element only when it has to, so both forms have to be read.
json parseTextArray(const std::string& in)
{
    json out = json::array();
    std::string cur;
    bool quoted = false, started = false;

    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const char c = in[i];
        if (!started)
        {
            if (c == '{')
                started = true;
            continue;
        }
        if (quoted)
        {
            if (c == '\\' && i + 1 < in.size())
                cur += in[++i];
            else if (c == '"')
                quoted = false;
            else
                cur += c;
            continue;
        }
        if (c == '"')
            quoted = true;
        else if (c == ',' || c == '}')
        {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
            if (c == '}')
                break;
        }
        else
            cur += c;
    }
    return out;
}

json parseRecord(const std::string& in)
{
    try
    {
        return json::parse(in);
    }
    catch (const std::exception&)
    {
        return json::object();
    }
}

// The feed writes these as arrays of {lang, value}, and sometimes as a bare string. Normalised to
// a plain array of strings so the page has one shape to render.
json proseField(const json& record, const char* key)
{
    json out = json::array();
    if (!record.contains(key))
        return out;
    const auto& v = record[key];
    if (v.is_string())
    {
        out.push_back(v.get<std::string>());
        return out;
    }
    if (!v.is_array())
        return out;
    for (const auto& e : v)
    {
        if (e.is_string())
            out.push_back(e.get<std::string>());
        else if (e.is_object() && e.contains("value") && e["value"].is_string())
            out.push_back(e["value"].get<std::string>());
    }
    return out;
}

json stringArray(const json& record, const char* key)
{
    json out = json::array();
    if (!record.contains(key))
        return out;
    const auto& v = record[key];
    if (v.is_string())
        out.push_back(v.get<std::string>());
    else if (v.is_array())
        for (const auto& e : v)
            if (e.is_string())
                out.push_back(e.get<std::string>());
    return out;
}

}

void AdvisoryController::overview(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                  pz::http::HttpResponse& resp)
{
    LOG_DEBUG("AdvisoryController::overview"); // debug

    (void)sm;
    (void)req;

    json body;
    json counts = {{"total", 0}, {"critical", 0}, {"high", 0}, {"medium", 0}, {"low", 0}, {"none", 0}};
    body["updated_30d"] = 0;
    body["last_sync"] = "";
    body["last_sync_status"] = "";

    try
    {
        auto& db = pz::db::Database::instance();

        // One pass for the whole distribution. Five counting queries would each scan the same rows
        // and could disagree with one another if a collection landed between them.
        for (const auto& r : db.queryRows("SELECT severity, count(*) FROM security_advisory GROUP BY severity"))
        {
            if (r.size() < 2 || !isSeverity(r[0]))
                continue;
            const int n = std::stoi(r[1]);
            counts[r[0]] = n;
            counts["total"] = counts["total"].get<int>() + n;
        }

        // Two different facts live in this table and only one of them belongs on screen.
        //
        //   source_updated > published_at   the VENDOR has revised the advisory. A property of the
        //                                   document, true of 149 of the 588 published so far, and
        //                                   what an operator means by "when was this last updated".
        //   revised_at                      the content changed BETWEEN TWO OF OUR COLLECTIONS.
        //                                   A property of this appliance's watch, which is what
        //                                   the alert sweep fires on — and which is necessarily
        //                                   empty on a fresh install, however much the vendor has
        //                                   edited.
        //
        // The page reports the first. Reporting the second made every advisory read as never
        // touched on day one, which is not what the table says.
        if (const auto n = db.queryScalar("SELECT count(*) FROM security_advisory "
                                          "WHERE source_updated > published_at "
                                          "  AND source_updated >= now() - interval '30 days'"))
            body["updated_30d"] = std::stoi(*n);

        const auto sync = db.queryRows(std::string("SELECT to_char(finished_at, '") + kTs + "'), status, "
                                       "listed, stored, revised, error FROM security_advisory_sync WHERE oid = 1");
        if (!sync.empty() && sync[0].size() >= 6)
        {
            body["last_sync"] = sync[0][0];
            body["last_sync_status"] = sync[0][1];
            body["listed"] = sync[0][2].empty() ? 0 : std::stoi(sync[0][2]);
            body["stored"] = sync[0][3].empty() ? 0 : std::stoi(sync[0][3]);
            body["revised"] = sync[0][4].empty() ? 0 : std::stoi(sync[0][4]);
            body["error"] = sync[0][5];
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("advisory overview failed (error={})", e.what());
        return fill(resp, 500, json{{"error", "failed to read advisories"}}.dump());
    }

    body["counts"] = std::move(counts);
    fill(resp, 200, body.dump());
}

void AdvisoryController::list(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                              pz::http::HttpResponse& resp)
{
    LOG_DEBUG("AdvisoryController::list"); // debug
    (void)sm;

    const std::string severity = queryParam(req.target, "severity");
    const std::string product = queryParam(req.target, "product");
    const std::string q = queryParam(req.target, "q");
    const std::string sort = queryParam(req.target, "sort");
    const int window = intParam(req.target, "window", 0, 0, 3650);
    const int limit = intParam(req.target, "limit", kDefaultLimit, 1, kMaxLimit);
    const int offset = intParam(req.target, "offset", 0, 0, 1000000);

    // Every filter is a parameter, and an unset one is passed as the empty string with the
    // predicate written to ignore it. One statement serves every combination that way — no string
    // concatenation of user input, and the planner sees one query shape.
    const std::string where =
        "WHERE ($1 = '' OR severity = $1) "
        "  AND ($2 = '' OR products @> ARRAY[$2]::text[]) "
        "  AND ($3 = '' OR id ILIKE '%' || $3 || '%' OR title ILIKE '%' || $3 || '%') "
        "  AND ($4 = '0' OR coalesce(source_updated, published_at) >= now() - ($4 || ' days')::interval) ";

    // The three the page offers. Each falls back to the activity instant so the order is total —
    // a severity sort that left ties unordered would shuffle rows between pages.
    std::string order = "ORDER BY coalesce(source_updated, published_at) DESC NULLS LAST, id DESC";
    if (sort == "published")
        order = "ORDER BY published_at DESC NULLS LAST, id DESC";
    else if (sort == "severity")
        order = "ORDER BY CASE severity WHEN 'critical' THEN 0 WHEN 'high' THEN 1 WHEN 'medium' THEN 2 "
                "WHEN 'low' THEN 3 ELSE 4 END, base_score DESC NULLS LAST, "
                "coalesce(source_updated, published_at) DESC NULLS LAST, id DESC";

    const std::string sev = isSeverity(severity) ? severity : std::string();
    const std::string term = q.size() >= kMinQuery ? q : std::string();
    const std::string days = window > 0 ? std::to_string(window) : std::string("0");

    json body;
    body["items"] = json::array();
    body["products"] = json::array();
    body["total"] = 0;
    body["offset"] = offset;
    body["limit"] = limit;

    try
    {
        auto& db = pz::db::Database::instance();

        if (const auto n = db.queryScalar("SELECT count(*) FROM security_advisory " + where,
                                          {sev, product, term, days}))
            body["total"] = std::stoi(*n);

        const auto rows = db.queryRows(
            std::string("SELECT id, title, severity, base_score, "
                        " to_char(published_at, '") + kTs + "'), to_char(source_updated, '" + kTs + "'), "
            // `revised` is the vendor's own revision, not this appliance's observation of one —
            // see the note in overview() for why those are different and which one a reader wants.
            " products, has_fix, (source_updated > published_at) "
            "FROM security_advisory " + where + order + " LIMIT $5::int OFFSET $6::int",
            {sev, product, term, days, std::to_string(limit), std::to_string(offset)});

        for (const auto& r : rows)
        {
            if (r.size() < 9)
                continue;
            json a;
            a["id"] = r[0];
            a["title"] = r[1];
            a["severity"] = r[2];
            if (r[3].empty())
                a["base_score"] = nullptr;
            else
                a["base_score"] = std::stod(r[3]);
            a["published"] = r[4];
            a["updated"] = r[5];
            a["products"] = parseTextArray(r[6]);
            a["has_fix"] = (r[7] == "t");
            a["revised"] = (r[8] == "t");
            body["items"].push_back(std::move(a));
        }

        // The whole vocabulary, not this page's — it fills a filter that has to be able to offer a
        // product the current page happens not to show.
        for (const auto& r : db.queryRows("SELECT DISTINCT unnest(products) AS p FROM security_advisory "
                                          "ORDER BY p"))
            if (!r.empty() && !r[0].empty())
                body["products"].push_back(r[0]);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("advisory list failed (error={})", e.what());
        return fill(resp, 500, json{{"error", "failed to read advisories"}}.dump());
    }

    fill(resp, 200, body.dump());
}

void AdvisoryController::detail(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                pz::http::HttpResponse& resp)
{
    LOG_DEBUG("AdvisoryController::detail"); // debug
    (void)sm;

    const std::string id = queryParam(req.target, "id");
    if (id.empty())
        return fill(resp, 400, json{{"error", "id is required"}}.dump());

    json body;
    try
    {
        auto& db = pz::db::Database::instance();
        const auto rows = db.queryRows(
            std::string("SELECT id, title, severity, base_score, threat_score, "
                        " to_char(published_at, '") + kTs + "'), to_char(source_updated, '" + kTs + "'), "
            " record_json FROM security_advisory WHERE id = $1",
            {id});

        if (rows.empty() || rows[0].size() < 8)
            return fill(resp, 404, json{{"error", "no such advisory"}}.dump());

        const auto& r = rows[0];
        const json record = parseRecord(r[7]);

        body["id"] = r[0];
        body["title"] = r[1];
        body["severity"] = r[2];
        body["base_score"] = r[3].empty() ? json(nullptr) : json(std::stod(r[3]));
        body["threat_score"] = r[4].empty() ? json(nullptr) : json(std::stod(r[4]));
        body["published"] = r[5];
        body["updated"] = r[6];
        body["url"] = "https://security.paloaltonetworks.com/" + r[0];
        body["aliases"] = json::array();

        // Read out of the stored record rather than promoted to columns: nothing filters or orders
        // on them, and the vendor adds fields here without notice.
        json cvss;
        for (const char* k : {"AV", "AC", "PR", "UI", "C", "I", "A"})
            if (record.contains(k) && record[k].is_string())
                cvss[k] = record[k];
        body["cvss"] = std::move(cvss);

        // The feed gives three parallel arrays — the product line, the versions it affects, the
        // versions that fix it — and they are only meaningful read together, index by index.
        const json versions = stringArray(record, "version");
        const json affected = stringArray(record, "affected");
        const json fixed = stringArray(record, "fixed");
        json table = json::array();
        for (std::size_t i = 0; i < versions.size(); ++i)
        {
            json v;
            v["product"] = versions[i];
            v["affected"] = i < affected.size() ? affected[i] : json("");
            const std::string fx = i < fixed.size() && fixed[i].is_string() ? fixed[i].get<std::string>() : "";
            v["fixed"] = (fx == "None") ? "" : fx;
            table.push_back(std::move(v));
        }
        body["versions"] = std::move(table);

        body["problem"] = proseField(record, "problem");
        body["solution"] = proseField(record, "solution");
        body["work_around"] = proseField(record, "work_around");
    }
    catch (const std::exception& e)
    {
        LOG_WARN("advisory detail failed (id={}, error={})", id, e.what());
        return fill(resp, 500, json{{"error", "failed to read advisory"}}.dump());
    }

    fill(resp, 200, body.dump());
}

void AdvisoryController::refresh(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                 pz::http::HttpResponse& resp)
{
    LOG_DEBUG("AdvisoryController::refresh"); // debug
    (void)req;

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Collectord);
    msg->setCmd(pz::ipc::IpcCmd::AdvisoryRefreshRequest);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload({});

    sm.txRouter().handleIpcMessage(std::move(msg));

    LOG_INFO("advisory refresh requested");
    fill(resp, 202, json{{"status", "requested"}}.dump());
}

}
