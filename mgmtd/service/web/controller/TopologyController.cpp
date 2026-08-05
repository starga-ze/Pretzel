#include "service/web/controller/TopologyController.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

#include "http/HttpMessage.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pz::mgmtd
{

using json = nlohmann::json;

// GET /api/topology?site=<oid>
//
// What is deliberately NOT done: blanking the picture while refreshing. A composition that lands in
// 20ms would turn every periodic refresh into a flicker. `pending` and "there is nothing to draw"
// are therefore separate facts, and the page shows its composing state only when it has neither.
void TopologyController::siteTopology(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                                    pz::http::HttpResponse& resp)
{
    const std::string siteOid = queryParam(req.target, "site");

    // Ask only when the answer would be new: nothing cached, or what is cached has aged past the
    // page's own refresh interval. One outstanding request per site — a burst of polls (several tabs,
    // a held-down refresh) must not become a burst of compositions.
    constexpr auto kFreshFor = std::chrono::seconds(50);
    const bool haveFresh = sm.topologyFresh(siteOid, kFreshFor);

    if (!haveFresh && !sm.topologyRequested(siteOid))
    {
        json ask;
        ask["site"] = siteOid;
        const std::string body = ask.dump();

        auto msg = std::make_unique<pz::ipc::IpcMessage>();
        msg->setSrc(pz::ipc::IpcDaemon::Mgmtd);
        msg->setDst(pz::ipc::IpcDaemon::Topologyd);
        msg->setCmd(pz::ipc::IpcCmd::TopologyRequest);
        msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
        msg->setPayload(std::vector<std::uint8_t>(body.begin(), body.end()));

        sm.txRouter().handleIpcMessage(std::move(msg));
        sm.markTopologyRequested(siteOid);
    }

    if (const std::string* model = sm.topology(siteOid))
    {
        // Spliced in rather than re-serialising: the model is already a JSON string and can be tens
        // of kilobytes, and this runs on the request path.
        std::string out = *model;
        const auto brace = out.find('{');
        if (brace != std::string::npos)
            out.insert(brace + 1, haveFresh ? "\"pending\":false," : "\"pending\":true,");
        return fill(resp, 200, out);
    }

    // Nothing composed for this site yet. A well-formed empty model rather than an error: the page
    // renders its composing state from `pending` and never has to special-case a 503.
    json out;
    out["site"] = siteOid;
    out["pending"] = true;
    out["sites"] = json::array();
    out["sase"] = {{"tenants", json::array()}};
    out["ngfw"] = {{"devices", json::array()}};
    out["sources"] = json::object();
    fill(resp, 200, out.dump());
}

void TopologyController::onTopologyResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg)
{
    const auto& pl = msg.getPayload();
    if (pl.empty())
    {
        LOG_WARN("empty topology response — dropping");
        return;
    }

    std::string body(pl.begin(), pl.end());

    // The site is read back out of the model rather than tracked against the request, because the
    // model is the authority on what it describes. A body that will not parse is dropped rather than
    // filed under a guessed site: serving a malformed model would replace a good picture with a
    // broken one, where dropping it leaves the last good answer up and the page merely goes stale.
    std::string site;
    try
    {
        site = json::parse(body).value("site", std::string());
    }
    catch (const std::exception& e)
    {
        LOG_WARN("topology response was not JSON ({}) — dropping", e.what());
        return;
    }

    LOG_DEBUG("topology model filed (site={}, bytes={})", site.empty() ? "all" : site, body.size());
    sm.setTopology(site, std::move(body));
}

}
