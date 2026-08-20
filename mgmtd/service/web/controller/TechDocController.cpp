#include "service/web/controller/TechDocController.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"
#include "grpc/GrpcMessage.h"

#include "http/HttpMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
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
    const std::string product = queryParam(req.target, "product");
    const std::string docset = queryParam(req.target, "docset");
    if (!validScope(product))
        return fill(resp, 400, R"({"error":"invalid product"})");
    // The docset is a path segment like the product, and reaches the same SQL filter.
    if (!validScope(docset))
        return fill(resp, 400, R"({"error":"invalid docset"})");

    const std::uint32_t ticket = sm.nextChatTicket();
    sm.txRouter().handleGrpcMessage(
        GrpcMessage::corpus(GrpcCmd::CorpusDocuments, ticket, product, docset));
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

    sm.txRouter().handleGrpcMessage(GrpcMessage::corpus(GrpcCmd::CorpusRefresh, 0, scope));

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
