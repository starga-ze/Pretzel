#include "service/web/controller/InventoryController.h"

#include "service/web/WebResponse.h"
#include "service/web/WebRouter.h"

#include "db/Database.h"
#include "http/HttpMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pz::mgmtd
{

namespace
{

using json = nlohmann::json;

void handleInventoryStatus(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    (void)sm;
    (void)req;

    // engined projects the configured devices into `devices` and marks each NGFW (IP-based)
    // row 'active' when ICMP answers; return the set of currently-active targets (IPs).
    json alive = json::array();
    try
    {
        auto& db = pz::db::Database::instance();
        for (const auto& row : db.queryRows("SELECT target FROM devices WHERE status = 'active' AND target IS NOT NULL"))
        {
            if (!row.empty() && !row[0].empty())
                alive.push_back(row[0]);
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN("device status query failed: {}", e.what());
    }

    fill(resp, 200, json{{"alive", std::move(alive)}}.dump());
}

}

void InventoryController::registerRoutes(WebRouter& router)
{
    router.get("/api/inventory/status", WebRouter::Access::Authenticated, &handleInventoryStatus);
}

}
