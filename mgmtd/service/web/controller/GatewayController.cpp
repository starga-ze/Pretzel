#include "service/web/controller/GatewayController.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

#include "db/Database.h"
#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pz::mgmtd
{

using json = nlohmann::json;

namespace
{

// A gateway key is a token, not a document. The cap is here rather than in inferd because a value
// this size should never have crossed the IPC socket: frames are bounded (IPC_MAX_FRAME_SIZE), so
// an oversized key would surface as a transport error instead of the "too long" the operator needs.
constexpr std::size_t kMaxKeyChars = 4096;

// One gateway is configured today. Named rather than assumed so the row this controller reads and
// the row inferd writes cannot drift apart silently, and so a second gateway is a new constant
// rather than a schema change.
constexpr const char* kDefaultGatewayId = "portkey";

}

void GatewayController::credentialStore(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                        pz::http::HttpResponse& resp)
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

    const std::string apiKey = input.value("api_key", std::string());
    if (apiKey.empty())
        return fill(resp, 400, R"({"error":"api_key is required"})");

    if (apiKey.size() > kMaxKeyChars)
        return fill(resp, 413, R"({"error":"api_key is too long"})");

    std::string id = input.value("id", std::string());
    if (id.empty())
        id = kDefaultGatewayId;

    // The connector tests' ticket space, reused deliberately: this is the same "hand it to a daemon,
    // poll for the outcome" exchange, and a second parallel ticket store would only give the browser
    // a second endpoint to learn for an identical shape of answer.
    const std::uint32_t ticket = sm.nextApiTestTicket();

    json ask;
    ask["id"] = id;
    ask["api_key"] = apiKey;
    const std::string payload = ask.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Inferd);
    msg->setCmd(pz::ipc::IpcCmd::GatewayCredentialStoreRequest);
    msg->setSeqNo(ticket);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));

    // The key is not logged, and neither is its length — that alone narrows a guess. The ticket is
    // enough to follow the store through.
    LOG_INFO("gateway credential store delegated to inferd (ticket={}, id={})", ticket, id);

    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

void GatewayController::status(MgmtdServiceManager&, const pz::http::HttpRequest& req,
                               pz::http::HttpResponse& resp)
{
    std::string id = queryParam(req.target, "id");
    if (id.empty())
        id = kDefaultGatewayId;

    // key_enc is read as a NULL test only. Returning the ciphertext would hand every console user
    // the half of the secret that a stolen credentials.key completes, for no operational gain —
    // the page only ever renders whether a key is present.
    const auto rows = pz::db::Database::instance().queryRows(
        "SELECT (key_enc IS NOT NULL), "
        "COALESCE(to_char(last_test_at, 'YYYY-MM-DD\"T\"HH24:MI:SSOF'), ''), "
        "COALESCE(last_test_ok::text, ''), COALESCE(last_test_note, '') "
        "FROM ai_gateway_credential_state WHERE id = $1",
        {id});

    json out{{"id", id}, {"configured", false}};

    if (!rows.empty() && rows[0].size() >= 4)
    {
        const auto& r = rows[0];
        out["configured"] = (r[0] == "t" || r[0] == "true");
        out["last_test_at"] = r[1];
        if (!r[2].empty())
            out["last_test_ok"] = (r[2] == "t" || r[2] == "true");
        out["last_test_note"] = r[3];
    }

    fill(resp, 200, out.dump());
}

void GatewayController::onCredentialStoreResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg)
{
    const auto& pl = msg.getPayload();
    if (pl.empty())
    {
        LOG_WARN("empty gateway credential store response (ticket={}) — dropping", msg.getSeqNo());
        return;
    }

    // Stored verbatim: inferd composed the complete outcome (or the complete reason there is none),
    // and re-deriving any of it here would put credential logic back in mgmtd.
    sm.setApiTestResult(msg.getSeqNo(), std::string(pl.begin(), pl.end()));

    LOG_DEBUG("gateway credential store filed (ticket={}, bytes={})", msg.getSeqNo(), pl.size());
}

}
