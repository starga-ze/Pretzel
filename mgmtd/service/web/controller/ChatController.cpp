#include "service/web/controller/ChatController.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

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

// A turn is a person typing, not a file upload. The cap is here rather than in inferd because a
// request this size should never have crossed the IPC socket in the first place — and IPC frames
// are bounded (IPC_MAX_FRAME_SIZE), so an oversized turn would fail as a transport error rather
// than as the "too long" the operator needs to read.
constexpr std::size_t kMaxMessageChars = 32 * 1024;

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

    const std::uint32_t ticket = sm.nextChatTicket();

    // Delegated through the router, same as the old IPC path — the controller does not know or
    // care that the transport underneath is now gRPC to the pretzel-ai service. The turn is
    // fire-and-forget: the answer is filed under `ticket` when it lands (GrpcClientHandler), and
    // result() below hands it back on the next poll. No system prompt is sent; the gateway uses
    // its configured default.
    sm.txRouter().handleGrpcMessage(ticket, model, message, std::string());

    // The message itself is not logged. It is whatever an employee typed, and this log is read by
    // people who have no business reading that; the ticket is enough to follow a turn through.
    LOG_INFO("chat turn delegated to pretzel-ai (ticket={}, model={}, chars={})", ticket,
             model.empty() ? "default" : model, message.size());

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
        return fill(resp, 200, json{{"status", "pending"}}.dump());

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
