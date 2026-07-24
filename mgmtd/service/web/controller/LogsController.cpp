#include "service/web/controller/LogsController.h"

#include "service/web/WebResponse.h"
#include "service/web/WebRouter.h"

#include "db/Database.h"
#include "http/HttpMessage.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace pz::mgmtd
{

namespace
{

using json = nlohmann::json;

std::string queryParam(const std::string& target, const std::string& key)
{
    const auto q = target.find('?');
    if (q == std::string::npos)
        return {};
    std::string qs = target.substr(q + 1);
    std::istringstream ss(qs);
    std::string token;
    while (std::getline(ss, token, '&'))
    {
        const auto eq = token.find('=');
        if (eq == std::string::npos)
            continue;
        if (token.substr(0, eq) == key)
            return token.substr(eq + 1);
    }
    return {};
}

// Percent-decode a query value ('+' is a space) so a search term can carry spaces and punctuation.
std::string urlDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == '+')
        {
            out.push_back(' ');
        }
        else if (in[i] == '%' && i + 2 < in.size() && std::isxdigit((unsigned char)in[i + 1]) &&
                 std::isxdigit((unsigned char)in[i + 2]))
        {
            out.push_back(static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        }
        else
        {
            out.push_back(in[i]);
        }
    }
    return out;
}

const std::array<const char*, 7> kKnownDaemons = {"ipcd", "engined", "mgmtd", "authd", "icmpd", "scand", "topologyd"};

// Severity threshold: a level filter of "warn" returns warn and worse. Accepts a name or a raw digit;
// returns -1 for "no filter" / unrecognised.
int levelNameToNum(const std::string& s)
{
    if (s.empty())
        return -1;
    if (std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }))
        return std::stoi(s);
    std::string u;
    for (char c : s)
        u += static_cast<char>(std::toupper((unsigned char)c));
    if (u == "TRACE")
        return 0;
    if (u == "DEBUG")
        return 1;
    if (u == "INFO")
        return 2;
    if (u == "WARN" || u == "WARNING")
        return 3;
    if (u == "ERROR" || u == "ERR")
        return 4;
    if (u == "CRITICAL" || u == "CRIT")
        return 5;
    return -1;
}

const char* levelNumToName(int n)
{
    switch (n)
    {
    case 0:
        return "TRACE";
    case 1:
        return "DEBUG";
    case 2:
        return "INFO";
    case 3:
        return "WARN";
    case 4:
        return "ERROR";
    case 5:
        return "CRITICAL";
    default:
        return "INFO";
    }
}

// GET /api/logs?daemon=&level=&q=&before=<oid>&limit=
//   daemon : restrict to one daemon (empty = all)
//   level  : severity threshold — this level and worse (name or digit; empty = all)
//   q      : case-insensitive substring match on the message
//   before : keyset cursor — return rows strictly older than this oid (empty = newest)
//   limit  : page size, 1..500 (default 100)
// Rows come back newest-first with a next_cursor for the following page — all filtering and paging is
// done in postgres, so the client just renders.
void handleLogs(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    const std::string& target = req.target;

    const std::string daemon = queryParam(target, "daemon");
    if (!daemon.empty() &&
        std::none_of(kKnownDaemons.begin(), kKnownDaemons.end(), [&](const char* d) { return daemon == d; }))
        return fill(resp, 400, json{{"error", "unknown daemon"}}.dump());

    const int levelNum = levelNameToNum(queryParam(target, "level"));
    const std::string q = urlDecode(queryParam(target, "q"));
    const std::string before = queryParam(target, "before");

    int limit = 100;
    {
        const auto s = queryParam(target, "limit");
        if (!s.empty())
        {
            try
            {
                limit = std::stoi(s);
            }
            catch (...)
            {
            }
        }
        limit = std::clamp(limit, 1, 500);
    }

    // All predicates are optional and collapse to TRUE when their parameter is empty, so one prepared
    // shape serves every filter combination.
    const auto rows = pz::db::Database::instance().queryRows(
        "SELECT oid, to_char(ts, 'YYYY-MM-DD HH24:MI:SS.MS'), daemon, level, coalesce(loc, ''), message "
        "FROM system_log "
        // NULLIF guards the casts: an empty filter param would fail as ''::int / ''::bigint, so it is
        // turned into NULL (never evaluated anyway once the '$n = '''' OR' short-circuits).
        "WHERE ($1 = '' OR daemon = $1) "
        "AND ($2 = '' OR level >= NULLIF($2, '')::int) "
        "AND ($3 = '' OR message ILIKE '%' || $3 || '%') "
        "AND ($4 = '' OR oid < NULLIF($4, '')::bigint) "
        "ORDER BY oid DESC LIMIT $5::int",
        {daemon, levelNum < 0 ? std::string() : std::to_string(levelNum), q, before, std::to_string(limit)});

    json arr = json::array();
    for (const auto& r : rows)
    {
        if (r.size() < 6)
            continue;
        int lvl = 2;
        try
        {
            lvl = std::stoi(r[3]);
        }
        catch (...)
        {
        }
        arr.push_back({{"oid", r[0]},
                       {"ts", r[1]},
                       {"daemon", r[2]},
                       {"level", levelNumToName(lvl)},
                       {"loc", r[4]},
                       {"message", r[5]}});
    }

    json body;
    body["rows"] = std::move(arr);
    // A full page implies more may follow; the cursor is the oldest oid returned.
    if (!rows.empty() && static_cast<int>(rows.size()) == limit)
        body["next_cursor"] = rows.back()[0];
    else
        body["next_cursor"] = nullptr;

    fill(resp, 200, body.dump());
}

}

void LogsController::registerRoutes(WebRouter& router)
{
    router.getPrefix("/api/logs", WebRouter::Access::Authenticated, &handleLogs);
}

}
