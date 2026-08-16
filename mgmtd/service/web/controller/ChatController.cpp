#include "service/web/controller/ChatController.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
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

// ragd — corpus retrieval, loopback only. It lives outside the daemons because turning a question
// into a vector needs the embedding model, which is Python; everything downstream of that (pgvector,
// ranking) a C++ daemon could do perfectly well, but the query has to be embedded first.
//
// Not in startup-config yet. It is a fixed loopback endpoint with no deployment variation today,
// and a config knob nobody turns is a knob that goes stale; it earns a config entry the day ragd
// runs somewhere other than here.
constexpr const char* kRagHost = "127.0.0.1";
constexpr const char* kRagPort = "8077";

// Short on purpose. Retrieval is tens of milliseconds; anything approaching this means ragd is down
// or wedged, and the operator is better served by a fast, clear failure than by a page that hangs.
constexpr int kRagTimeoutMs = 4000;

// Retrieval is a read against a fixed corpus, so the ceiling exists to bound the response and the
// prompt built from it, not to protect ragd — which clamps k on its own side regardless.
constexpr int kMaxK = 20;

// A synchronous loopback exchange. Blocking the shared loop is a real cost and the reason this is
// bounded so tightly; see the note on ChatController::retrieve for why it is acceptable here and
// what to do if that stops being true.
struct RagReply
{
    bool ok{false};
    int status{0};
    std::string body;
    std::string error;
};

RagReply postToRag(const std::string& payload)
{
    namespace beast = boost::beast;
    namespace asio = boost::asio;
    using tcp = asio::ip::tcp;

    RagReply out;
    try
    {
        asio::io_context ioc;
        tcp::socket sock(ioc);

        // Deadline covers connect, write and read together — a per-operation timeout would let a
        // peer that trickles bytes hold the loop open indefinitely.
        bool timedOut = false;
        asio::steady_timer deadline(ioc);
        deadline.expires_after(std::chrono::milliseconds(kRagTimeoutMs));
        deadline.async_wait([&](const boost::system::error_code& ec) {
            if (!ec) { timedOut = true; boost::system::error_code ignored; sock.close(ignored); }
        });

        tcp::resolver resolver(ioc);
        const auto endpoints = resolver.resolve(kRagHost, kRagPort);

        boost::system::error_code ec;
        asio::async_connect(sock, endpoints,
            [&](const boost::system::error_code& e, const tcp::endpoint&) { ec = e; });
        ioc.run_one();
        if (ec) throw boost::system::system_error(ec);

        beast::http::request<beast::http::string_body> httpReq{beast::http::verb::post, "/retrieve", 11};
        httpReq.set(beast::http::field::host, kRagHost);
        httpReq.set(beast::http::field::content_type, "application/json");
        httpReq.body() = payload;
        httpReq.prepare_payload();

        beast::http::write(sock, httpReq);

        beast::flat_buffer buffer;
        beast::http::response<beast::http::string_body> httpResp;
        beast::http::read(sock, buffer, httpResp);

        deadline.cancel();
        boost::system::error_code ignored;
        sock.shutdown(tcp::socket::shutdown_both, ignored);

        if (timedOut)
        {
            out.error = "ragd timed out";
            return out;
        }

        out.ok = true;
        out.status = httpResp.result_int();
        out.body = std::move(httpResp.body());
    }
    catch (const std::exception& e)
    {
        out.error = e.what();
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

void ChatController::retrieve(MgmtdServiceManager&, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
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

    const std::string query = input.value("query", std::string());
    if (query.empty())
        return fill(resp, 400, R"({"error":"query is required"})");

    if (query.size() > kMaxMessageChars)
        return fill(resp, 413, R"({"error":"query is too long"})");

    int k = input.value("k", 5);
    if (k < 1) k = 1;
    if (k > kMaxK) k = kMaxK;

    json ask;
    ask["query"] = query;
    ask["k"] = k;
    if (input.contains("docset") && input["docset"].is_string())  ask["docset"]  = input["docset"];
    if (input.contains("version") && input["version"].is_string()) ask["version"] = input["version"];

    const RagReply reply = postToRag(ask.dump());

    // ragd being down is an ordinary operational state — the assistant page is reachable whether or
    // not the corpus service is running — so it is reported as such rather than as a 500 from mgmtd.
    if (!reply.ok)
    {
        LOG_WARN("retrieval unavailable: {}", reply.error);
        return fill(resp, 503, json{{"error", "corpus service unavailable"},
                                    {"code", "RAG_UNAVAILABLE"},
                                    {"detail", reply.error}}.dump());
    }

    if (reply.status != 200)
    {
        LOG_WARN("ragd returned HTTP {}", reply.status);
        return fill(resp, 502, json{{"error", "corpus service error"},
                                    {"code", "RAG_ERROR"},
                                    {"upstream_status", reply.status}}.dump());
    }

    json body = json::parse(reply.body, nullptr, false);
    if (body.is_discarded())
        return fill(resp, 502, R"({"error":"malformed answer from ragd","code":"RAG_ERROR"})");

    // The query is not logged, for the same reason send() does not log the turn.
    LOG_DEBUG("retrieval served (k={}, hits={})", k,
              body.contains("hits") ? body["hits"].size() : 0);

    fill(resp, 200, body.dump());
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
