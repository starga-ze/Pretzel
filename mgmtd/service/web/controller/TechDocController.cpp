#include "service/web/controller/TechDocController.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"
#include "grpc/GrpcMessage.h"

#include "http/HttpMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <charconv>
#include <string>

namespace pz::mgmtd
{

using json = nlohmann::json;

namespace
{

// A scope is one product slug from the sitemap ("ngfw", "pan-os"), or empty for the whole corpus.
// Bounded and character-checked here rather than trusted downstream: it is interpolated into the
// crawler's filter, and a slug is all it is ever allowed to be.
constexpr std::size_t kMaxScopeChars = 64;

// A search term is prose, not a slug, so it is bounded rather than character-checked: it reaches
// the store as a bound query parameter and never as SQL. The cap is what stops a pathological
// target turning into a corpus-wide scan per keystroke.
constexpr std::size_t kMaxQueryChars = 128;

bool validScope(const std::string& scope)
{
    if (scope.size() > kMaxScopeChars)
        return false;
    for (char c : scope)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok)
            return false;
    }
    return true;
}

std::string scopeOf(const pz::http::HttpRequest& req, bool& bad)
{
    bad = false;
    if (req.body.empty())
        return {};
    json input = json::parse(req.body, nullptr, false);
    if (input.is_discarded() || !input.is_object())
    {
        bad = true;
        return {};
    }
    if ((input.contains("scope") && !input["scope"].is_string()) ||
        (input.contains("dry_run") && !input["dry_run"].is_boolean()))
    {
        bad = true;
        return {};
    }
    std::string scope = input.value("scope", std::string());
    if (!validScope(scope))
        bad = true;
    return scope;
}

}

void TechDocController::status(MgmtdServiceManager& sm, const pz::http::HttpRequest&, pz::http::HttpResponse& resp)
{
    const std::uint32_t ticket = sm.nextChatTicket();
    sm.txRouter().handleGrpcMessage(GrpcMessage::corpus(GrpcCmd::CorpusStatus, ticket));
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void TechDocController::documents(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                  pz::http::HttpResponse& resp)
{
    // Two questions on one route, because both answer with the same document list and the
    // console shows them in the same place: `q` searches the corpus by URL and title, while
    // product/docset browse one book.
    const std::string query = queryParam(req.target, "q");
    if (query.size() > kMaxQueryChars)
        return fill(resp, 400, R"({"error":"query too long"})");

    // The slug rules apply to the browse fields, and only when they are the ones being read. A
    // search ignores them downstream, so validating whatever the console left in the URL would
    // refuse a perfectly good search on the strength of a field nothing is going to look at.
    std::string product;
    std::string docset;
    if (query.empty())
    {
        product = queryParam(req.target, "product");
        docset = queryParam(req.target, "docset");
        if (!validScope(product))
            return fill(resp, 400, R"({"error":"invalid product"})");
        // The docset is a path segment like the product, and reaches the same SQL filter.
        if (!validScope(docset))
            return fill(resp, 400, R"({"error":"invalid docset"})");
    }

    const std::string version = queryParam(req.target, "version");
    if (version.size() > 128 || version.find_first_of("\r\n") != std::string::npos)
        return fill(resp, 400, R"({"error":"invalid version"})");
    auto number = [&](const char* name, int fallback, int maximum) {
        const auto raw = queryParam(req.target, name);
        if (raw.empty()) return fallback;
        int value = 0;
        const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size()
            && value >= 0 && value <= maximum ? value : -1;
    };
    const int offset = number("offset", 0, 1000000);
    const int limit = number("limit", 100, 200);
    if (offset < 0 || limit < 1)
        return fill(resp, 400, R"({"error":"invalid pagination"})");
    const std::uint32_t ticket = sm.nextChatTicket();
    auto message = GrpcMessage::corpus(GrpcCmd::CorpusDocuments, ticket, product, docset, query);
    message.corpusVersion = version;
    message.corpusVersionSet = req.target.find("?version=") != std::string::npos
        || req.target.find("&version=") != std::string::npos;
    message.offset = offset;
    message.limit = limit;
    sm.txRouter().handleGrpcMessage(std::move(message));
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void TechDocController::exceptions(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                   pz::http::HttpResponse& resp)
{
    // A reason is one of the crawler's own enumerated names (navigation_only, http_404, …), so it
    // is checked against that alphabet rather than trusted: it selects rows, and a name is all it
    // is ever allowed to be.
    const std::string reason = queryParam(req.target, "reason");
    if (reason.size() > 64)
        return fill(resp, 400, R"({"error":"invalid reason"})");
    for (char c : reason)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return fill(resp, 400, R"({"error":"invalid reason"})");
    }

    auto number = [&](const char* name, long long fallback, long long maximum) {
        const auto raw = queryParam(req.target, name);
        if (raw.empty())
            return fallback;
        long long value = 0;
        const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size()
            && value >= 0 && value <= maximum ? value : -1LL;
    };
    // run 0 is not a run id, it is "whichever ran last" — the question a console asks by default.
    const long long run = number("run", 0, 1000000000);
    const long long offset = number("offset", 0, 1000000);
    const long long limit = number("limit", 100, 500);
    if (run < 0 || offset < 0 || limit < 1)
        return fill(resp, 400, R"({"error":"invalid pagination"})");

    const std::uint32_t ticket = sm.nextChatTicket();
    sm.txRouter().handleGrpcMessage(GrpcMessage::corpusExceptions(
        ticket, run, reason, static_cast<std::int32_t>(offset), static_cast<std::int32_t>(limit)));
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void TechDocController::result(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    const std::string raw = queryParam(req.target, "ticket");
    const auto ticket = static_cast<std::uint32_t>(std::strtoul(raw.c_str(), nullptr, 10));
    if (ticket == 0)
        return fill(resp, 400, R"({"error":"bad ticket"})");

    auto result = sm.takeChatResult(ticket);
    if (!result)
        return fill(resp, 200, json{{"status", "pending"}}.dump());

    json body = json::parse(*result, nullptr, false);
    if (body.is_discarded())
        return fill(resp, 500, R"({"status":"done","error":"malformed answer from pretzel-ai"})");

    body["status"] = "done";
    fill(resp, 200, body.dump());
}

void TechDocController::refresh(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    bool bad = false;
    const std::string scope = scopeOf(req, bad);
    if (bad)
        return fill(resp, 400, R"({"error":"invalid scope"})");

    // 409 rather than a queue. The card holds a window open for this and tells the operator not to
    // close it; a request that silently waited its turn would look to them like one that had hung.
    //
    // The service manager decides, not the transport: "is a refresh running" is state, and a
    // handler that owned the answer would be a handler holding domain truth.
    if (!sm.beginCorpusRefresh())
        return fill(resp, 409, R"({"error":"a refresh is already running"})");

    auto message = GrpcMessage::corpus(GrpcCmd::CorpusRefresh, 0, scope);
    if (!req.body.empty())
        message.dryRun = json::parse(req.body).value("dry_run", false);
    sm.txRouter().handleGrpcMessage(std::move(message));

    LOG_INFO("tech-doc refresh started (scope={})", scope.empty() ? "all" : scope);
    fill(resp, 202, json{{"started", true}}.dump());
}

void TechDocController::cancel(MgmtdServiceManager& sm, const pz::http::HttpRequest&, pz::http::HttpResponse& resp)
{
    if (!sm.corpusRefreshing())
        return fill(resp, 409, R"({"error":"no refresh is running"})");

    sm.txRouter().handleGrpcMessage(GrpcMessage::corpus(GrpcCmd::CorpusCancel, 0));
    LOG_INFO("tech-doc refresh cancellation requested");
    fill(resp, 202, json{{"cancelling", true}}.dump());
}

void TechDocController::progress(MgmtdServiceManager& sm, const pz::http::HttpRequest&, pz::http::HttpResponse& resp)
{
    const std::string& latest = sm.corpusProgress();
    const bool running = sm.corpusRefreshing();
    LOG_DEBUG("tech-doc progress poll: running={} slot={}b", running, latest.size());

    // No refresh has run in this process. Distinct from "running with nothing to report": the
    // card renders its resting state for one and a progress bar for the other.
    if (latest.empty())
        return fill(resp, 200, json{{"running", running}, {"idle", true}}.dump());

    json body = json::parse(latest, nullptr, false);
    if (body.is_discarded())
        body = json::object();
    body["running"] = running;
    body["idle"] = false;
    fill(resp, 200, body.dump());
}

}
