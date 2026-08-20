#include "service/web/controller/ChatController.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"
#include "grpc/GrpcMessage.h"

#include "http/HttpMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace pz::mgmtd
{

using json = nlohmann::json;

namespace
{

// A turn is a person typing, not a file upload. The cap is here rather than in inferd because a
// request this size should never have crossed the IPC socket in the first place — and IPC frames
// are bounded (IPC_MAX_FRAME_SIZE), so an oversized turn would fail as a transport error rather
// than as the "too long" the operator needs to read.
constexpr std::size_t kMaxMessageChars = 32 * 1024;

// How far back a turn may carry its own conversation. Two caps, not one: a thread grows without
// bound while the model's context does not, and the guardrail scans whatever is sent, so an
// unbounded replay is a bill and a latency problem before it is a correctness one. Oldest turns
// are dropped rather than newest — recent context is what a follow-up question depends on.
constexpr std::size_t kMaxHistoryTurns = 20;
constexpr std::size_t kMaxHistoryChars = 64 * 1024;

// The browser sends the thread it is showing. It is not trusted to send it well: a role outside
// user/assistant is dropped rather than forwarded, because a mislabelled turn replayed as the
// other party rewrites what the model believes was already said.
std::vector<GrpcMessage::Turn> parseHistory(const json& input)
{
    std::vector<GrpcMessage::Turn> out;
    const auto it = input.find("history");
    if (it == input.end() || !it->is_array())
        return out;

    std::size_t chars = 0;
    for (const auto& entry : *it)
    {
        if (!entry.is_object())
            continue;

        GrpcMessage::Turn turn;
        turn.role = entry.value("role", std::string());
        turn.content = entry.value("content", std::string());

        if (turn.content.empty() || (turn.role != "user" && turn.role != "assistant"))
            continue;
        if (turn.content.size() > kMaxMessageChars)
            continue;

        chars += turn.content.size();
        out.push_back(std::move(turn));
    }

    // Trim from the front: the cap is reached by an old conversation, and the turns that matter to
    // the question being asked now are the ones at the end.
    while (out.size() > kMaxHistoryTurns)
        out.erase(out.begin());
    while (chars > kMaxHistoryChars && !out.empty())
    {
        chars -= out.front().content.size();
        out.erase(out.begin());
    }

    return out;
}

}

void ChatController::send(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    json input;
    try
    {
        input = json::parse(req.body);
    }
    catch (const std::exception&)
    {
        return fill(resp, 400, R"({"error":"invalid JSON body"})");
    }

    const std::string message = input.value("message", std::string());
    if (message.empty())
        return fill(resp, 400, R"({"error":"message is required"})");

    if (message.size() > kMaxMessageChars)
        return fill(resp, 413, R"({"error":"message is too long"})");

    const std::string model = input.value("model", std::string());
    const std::string sessionId = input.value("session_id", std::string());
    auto history = parseHistory(input);

    const std::uint32_t ticket = sm.nextChatTicket();

    // Delegated through the router, same as the old IPC path — the controller does not know or
    // care that the transport underneath is now gRPC to the pretzel-ai service. The turn is
    // fire-and-forget: the answer is filed under `ticket` when it lands (GrpcClientHandler), and
    // result() below hands it back on the next poll. No system prompt is sent; the gateway uses
    // its configured default.
    const std::size_t historyTurns = history.size();
    sm.txRouter().handleGrpcMessage(
        GrpcMessage::chat(ticket, model, message, std::string(), std::move(history), sessionId));

    // The message itself is not logged. It is whatever an employee typed, and this log is read by
    // people who have no business reading that; the ticket is enough to follow a turn through.
    // The history is not logged either, and for the same reason — only how much of it there was.
    LOG_INFO("chat turn delegated to pretzel-ai (ticket={}, model={}, chars={}, history={}, session={})",
             ticket, model.empty() ? "default" : model, message.size(), historyTurns,
             sessionId.empty() ? "none" : sessionId);

    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void ChatController::result(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    const std::string raw = queryParam(req.target, "ticket");
    const auto ticket = static_cast<std::uint32_t>(std::strtoul(raw.c_str(), nullptr, 10));

    if (ticket == 0)
        return fill(resp, 400, R"({"error":"bad ticket"})");

    // The turn is filed under its ticket by GrpcClientHandler::drain() once pretzel-ai answers;
    // until then the poll returns pending. Grounding/retrieval is no longer part of this path.
    auto result = sm.takeChatResult(ticket);
    if (!result)
    {
        // Still pending, but not necessarily silent: once the answer starts arriving the poll
        // carries what has been written so far. `text` is cumulative rather than incremental —
        // a poll can be missed or arrive out of order, and a client that appended deltas would
        // end up with a mangled answer, while one that replaces its buffer cannot.
        return fill(resp, 200, json{{"status", "pending"},
                                    {"text", sm.chatPartial(ticket)}}.dump());
    }

    json body = json::parse(*result, nullptr, false);
    if (body.is_discarded())
    {
        return fill(resp, 500, R"({"status":"done","ok":false,"code":"BAD_RESPONSE",)"
                               R"("error":"malformed answer from pretzel-ai"})");
    }

    body["status"] = "done";
    fill(resp, 200, body.dump());
}

}
