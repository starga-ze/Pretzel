#include "service/web/controller/ChatController.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <memory>
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

// Retrieval is a read against a fixed corpus, so the ceiling exists to bound the response and the
// prompt built from it, not to protect inferd — which clamps k on its own side regardless.
constexpr int kMaxK = 20;

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

    // Grounding is the caller's choice, not this controller's: the same appliance serves
    // questions the corpus covers and questions it does not, and a page that could not
    // turn it off would have no way to ask the second kind.
    const bool rag = input.value("rag", false);
    int topK = input.value("k", 5);
    if (topK < 1) topK = 1;
    if (topK > kMaxK) topK = kMaxK;

    const std::uint32_t ticket = sm.nextChatTicket();

    json ask;
    ask["message"] = message;
    ask["model"] = model;
    ask["rag"] = rag;
    if (rag)
    {
        ask["k"] = topK;
        // Passed through only when set, so inferd's own defaults stay in charge of the
        // unfiltered case rather than being overridden with an empty string here.
        if (input.contains("docset") && input["docset"].is_string())
            ask["docset"] = input["docset"];
        if (input.contains("version") && input["version"].is_string())
            ask["version"] = input["version"];
    }
    const std::string payload = ask.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Inferd);
    msg->setCmd(pz::ipc::IpcCmd::ChatRequest);
    msg->setSeqNo(ticket);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));

    // The message itself is not logged. It is whatever an employee typed, and this log is read by
    // people who have no business reading that; the ticket is enough to follow a turn through.
    LOG_INFO("chat turn delegated to inferd (ticket={}, model={}, chars={}, rag={})", ticket,
             model.empty() ? "default" : model, message.size(), rag ? "on" : "off");

    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void ChatController::result(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    const std::string raw = queryParam(req.target, "ticket");
    const auto ticket = static_cast<std::uint32_t>(std::strtoul(raw.c_str(), nullptr, 10));

    if (ticket == 0)
        return fill(resp, 400, R"({"error":"bad ticket"})");

    // Retrieval lands first on a grounded turn. Handing it over on its own poll is what
    // lets the page show the passages while the model is still working — the reader gets
    // to judge what was retrieved before the answer arrives to dress it up.
    auto retrieval = sm.takeRetrievalResult(ticket);

    auto result = sm.takeChatResult(ticket);
    if (!result)
    {
        json pending{{"status", "pending"}};
        if (retrieval)
        {
            json r = json::parse(*retrieval, nullptr, false);
            if (!r.is_discarded())
                pending["retrieval"] = std::move(r);
        }
        return fill(resp, 200, pending.dump());
    }

    json body = json::parse(*result, nullptr, false);
    if (body.is_discarded())
    {
        return fill(resp, 500, R"({"status":"done","ok":false,"code":"BAD_RESPONSE",)"
                               R"("error":"malformed answer from inferd"})");
    }

    body["status"] = "done";

    // Both arrived between two polls — the answer must not swallow the passages that
    // justified it, so they ride along on the same response.
    if (retrieval)
    {
        json r = json::parse(*retrieval, nullptr, false);
        if (!r.is_discarded())
            body["retrieval"] = std::move(r);
    }

    fill(resp, 200, body.dump());
}

void ChatController::onRetrieveResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg)
{
    const auto& pl = msg.getPayload();
    if (pl.empty())
    {
        LOG_WARN("empty retrieval response (ticket={}) — dropping", msg.getSeqNo());
        return;
    }

    sm.setRetrievalResult(msg.getSeqNo(), std::string(pl.begin(), pl.end()));
    LOG_DEBUG("retrieval filed (ticket={}, bytes={})", msg.getSeqNo(), pl.size());
}

void ChatController::onChatResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg)
{
    const auto& pl = msg.getPayload();
    if (pl.empty())
    {
        LOG_WARN("empty chat response (ticket={}) — dropping", msg.getSeqNo());
        return;
    }

    // Stored verbatim: inferd composed a complete answer document (or a complete explanation of why
    // there is none), and re-deriving any of it here would put inference logic back in mgmtd.
    sm.setChatResult(msg.getSeqNo(), std::string(pl.begin(), pl.end()));

    LOG_DEBUG("chat answer filed (ticket={}, bytes={})", msg.getSeqNo(), pl.size());
}

}
