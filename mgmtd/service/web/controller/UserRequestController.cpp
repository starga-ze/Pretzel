#include "service/web/controller/UserRequestController.h"

#include "service/web/WebUtil.h"

#include "config/Config.h"
#include "db/Database.h"
#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "service/MgmtdServiceManager.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>

namespace pz::mgmtd
{

using json = nlohmann::json;

namespace
{

// Postgres' own ISO rendering with the zone, the same format every other timestamp this console
// serves carries — the browser's parseTs expects it and pads the whole-hour offset back.
constexpr const char* kTs = "YYYY-MM-DD\"T\"HH24:MI:SSOF";

// A JSONB column straight out of Postgres is already JSON text; it goes into the response as a
// value rather than as a quoted string, so the page reads a document and not an escaped blob.
json findByOid(const json& arr, const std::string& oid)
{
    for (const auto& x : arr)
        if (x.is_object() && x.value("oid", std::string()) == oid)
            return x;
    return json::object();
}

// Same shape as ApiController's sendConnectorTest; kept here rather than shared because the two
// files are the only callers and a header to hold six lines would be the larger cost.
void sendSaseCall(MgmtdServiceManager& sm, std::uint32_t ticket, json call)
{
    const std::string payload = call.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
    msg->setDst(pz::ipc::IpcDaemon::Collectord);
    msg->setCmd(pz::ipc::IpcCmd::ApiSaseCallRequest);
    msg->setSeqNo(ticket);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));

    sm.txRouter().handleIpcMessage(std::move(msg));
}

json docOrNull(const std::string& raw)
{
    if (raw.empty())
        return nullptr;
    json v = json::parse(raw, nullptr, false);
    return v.is_discarded() ? json(nullptr) : v;
}

}

void UserRequestController::list(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                 pz::http::HttpResponse& resp)
{
    (void)sm;

    const std::string site = queryParam(req.target, "site");
    const std::string status = queryParam(req.target, "status");
    // Parsed here rather than through a shared helper because there is exactly one of them;
    // CollectionController's intParam is file-local to that translation unit and copying it to
    // reach one call site would be the worse trade.
    int limit = 200;
    if (const auto raw = queryParam(req.target, "limit"); !raw.empty())
    {
        try { limit = std::clamp(std::stoi(raw), 1, 1000); }
        catch (...) { limit = 200; }
    }

    // Site is required rather than defaulted to "everything": this page is reached through the
    // site switcher, and a queue that silently showed another customer's requests would be a
    // disclosure, not a convenience.
    if (site.empty())
        return fill(resp, 400, json{{"error", "site is required"}}.dump());

    json body;
    body["rows"] = json::array();

    try
    {
        const auto rows = pz::db::Database::instance().queryRows(
            std::string("SELECT id, COALESCE(type,''), COALESCE(status,''), "
                        "COALESCE(to_char(created_at, '")
                + kTs + "'),''), COALESCE(to_char(response_time, '" + kTs
                + "'),''), list_json::text, connector_oid, endpoint_oid "
                  "FROM pb_user_request "
                  "WHERE site_oid = $1 AND ($2 = '' OR status = $2) "
                  // Newest first, and NULLs last rather than first: a row whose createdAt the
                  // vendor omitted must not displace the ones that have one.
                  "ORDER BY created_at DESC NULLS LAST LIMIT $3::int",
            {site, status, std::to_string(limit)});

        for (const auto& r : rows)
        {
            if (r.size() < 8)
                continue;
            body["rows"].push_back({{"id", r[0]},
                                    {"type", r[1]},
                                    {"status", r[2]},
                                    {"created_at", r[3]},
                                    {"response_time", r[4]},
                                    // Every field the vendor returned, not a chosen subset: the
                                    // promoted columns above exist to be queried, this is what the
                                    // console shows.
                                    {"list", docOrNull(r[5])},
                                    {"connector_oid", r[6]},
                                    {"endpoint_oid", r[7]}});
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("user requests query failed: {}", e.what());
        return fill(resp, 500, json{{"error", "user requests unavailable"}}.dump());
    }

    fill(resp, 200, body.dump());
}


void UserRequestController::action(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                   pz::http::HttpResponse& resp)
{
    json input;
    if (!parseBody(req, resp, input))
        return;

    const std::string id = input.value("id", std::string());
    const std::string action = input.value("action", std::string());
    if (id.empty())
        return fill(resp, 400, json{{"error", "the request id is required"}}.dump());
    if (action != "approve" && action != "decline" && action != "revoke")
        return fill(resp, 400, json{{"error", "action must be approve, decline or revoke"}}.dump());

    // Where to call is derived from the row, never taken from the browser: a caller that could name
    // its own host and path would have an open proxy, not an approval button.
    std::string connectorOid;
    std::string endpointOid;
    try
    {
        const auto rows = pz::db::Database::instance().queryRows(
            "SELECT connector_oid, endpoint_oid FROM pb_user_request WHERE id = $1", {id});
        if (rows.empty() || rows[0].size() < 2)
            return fill(resp, 404, json{{"error", "no such request on this appliance"}}.dump());
        connectorOid = rows[0][0];
        endpointOid = rows[0][1];
    }
    catch (const std::exception& e)
    {
        LOG_WARN("user request lookup failed: {}", e.what());
        return fill(resp, 500, json{{"error", "the request could not be looked up"}}.dump());
    }

    const auto& conn = pz::config::Config::section(pz::config::scope::kPretzel, "connector");
    const json endpoint = findByOid(conn.value("endpoints", json::array()), endpointOid);
    const json connector = findByOid(conn.value("connectors", json::array()), connectorOid);

    std::string path = endpoint.value("path", std::string());
    const std::string host = endpoint.value("host", std::string());
    const std::string authProfile = connector.value("auth_profile", std::string());
    if (host.empty() || path.empty() || path.front() != '/' || authProfile.empty())
        return fill(resp, 409,
                    json{{"error", "the endpoint or credential this request came from is gone"}}.dump());

    if (path.back() == '/')
        path.pop_back();

    // The two vendor operations. Approve and decline are the same endpoint told apart by the body;
    // revoke is its own path and takes no action field — the spec marks one required, but it has no
    // such property and the vendor's own example omits it.
    // json::object(), not a default-constructed json — that one is NULL, and a revoke with no
    // comment assigns nothing, so it went out as the four bytes `null`. The vendor refused it at
    // the decoder ("{ expected: unexpected byte 110 'n' at 0") before any of its own rules ran.
    // Approve and decline hid the bug: they always assign `action`, which promotes null to object.
    json vendorBody = json::object();
    if (action == "revoke")
    {
        path += "/" + id + "/revoke";
        const std::string comment = input.value("comment", std::string());
        if (!comment.empty())
            vendorBody["revokerComment"] = comment;
    }
    else
    {
        path += "/" + id + "/action";
        vendorBody["action"] = action;
        const std::string comment = input.value("comment", std::string());
        if (!comment.empty())
            vendorBody["adminComment"] = comment;
        // Only meaningful on an approval, and left out entirely when unset so the tenant applies
        // whatever the rule says rather than a value this console invented.
        const std::string bypass = input.value("bypass", std::string());
        if (action == "approve" && !bypass.empty())
            vendorBody["adminBypassTimeframe"] = bypass;
    }

    json call;
    call["host"] = host;
    call["path"] = path;
    call["method"] = "POST";
    call["api_key_oid"] = authProfile;
    call["headers"] = endpoint.value("headers", json::array());
    call["body"] = vendorBody;

    const std::uint32_t ticket = sm.nextApiTestTicket();
    sendSaseCall(sm, ticket, std::move(call));

    LOG_INFO("user request action delegated to collectord (ticket={}, id={}, action={})", ticket, id, action);
    fill(resp, 202, json{{"ticket", ticket}, {"status", "pending"}}.dump());
}

}
