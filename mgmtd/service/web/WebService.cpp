#include "service/web/WebService.h"

#include "service/MgmtdServiceManager.h"

#include "service/web/controller/AuthController.h"
#include "service/web/controller/ConnectorController.h"
#include "service/web/controller/StatusController.h"
#include "service/web/controller/LogsController.h"
#include "service/web/controller/SettingsController.h"
#include "service/web/controller/SsoController.h"

#include "service/web/WebAction.h"
#include "service/web/WebEvent.h"
#include "service/web/WebUtil.h"

#include "router/MgmtdTxRouter.h"

#include "util/Logger.h"

#include <memory>
#include <string>
#include <utility>

namespace pz::mgmtd
{

namespace
{

constexpr const char* kPublicPages[] = {
    "/", "/index.html", "/css/main.css", "/js/main.js", "/js/login.js",
};

}

void WebService::handleEvent(MgmtdServiceManager& sm, const WebEvent& event)
{
    Response resp;
    route(sm, event.request(), resp);

    sm.postAction(std::make_unique<WebAction>(std::move(resp), event.sessionId()));
}

void WebService::handleAction(MgmtdServiceManager& sm, WebAction& action)
{
    sm.txRouter().handleHttpMessage(std::move(action.response()), action.sessionId());
}

void WebService::route(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    LOG_TRACE("HTTP request (method={}, target={})", req.method, req.target);

    if (!m_routesRegistered)
    {
        registerRoutes();
        m_routesRegistered = true;
    }

    if (m_router.dispatch(sm, req, resp))
        return;

    // Nothing in the table claimed it — a static file (everything outside /api).
    if (isStaticTarget(req.target))
        handleStaticFallback(sm, req, resp);
}

// The route table. Each domain owns a controller under controller/ that registers its own routes;
// the router enforces the session and must-change-password gates per route, which is why no handler
// re-checks auth. Only /metrics and static serving stay on the service itself.
void WebService::registerRoutes()
{
    using Access = WebRouter::Access;

    // ── Public ──────────────────────────────────────────────────────────────────────────────
    m_router.get("/metrics", Access::Public, &WebService::handleMetric);
    m_router.get("/health", Access::Public, &WebService::handleHealth);

    // ── Domain controllers ────────────────────────────────────────────────────────────────────
    AuthController::registerRoutes(m_router);
    SsoController::registerRoutes(m_router);
    SettingsController::registerRoutes(m_router);
    StatusController::registerRoutes(m_router);
    ConnectorController::registerRoutes(m_router);
    LogsController::registerRoutes(m_router);
}

void WebService::handleStaticFallback(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    bool isPublic = false;
    for (const auto* p : kPublicPages)
    {
        if (req.target == p)
        {
            isPublic = true;
            break;
        }
    }

    if (!isPublic && !sm.authService().validateSession(sessionCookie(req)))
    {
        resp.status = 302;
        resp.contentType = "text/plain; charset=utf-8";
        resp.body.clear();
        resp.location = "/index.html";
        return;
    }

    handleStatic(sm, req, resp);
}

void WebService::handleMetric(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    fill(resp, 200, sm.metricService().renderPrometheus(), "text/plain; version=0.0.4; charset=utf-8");
}

// Unauthenticated liveness probe. Always 200 while mgmtd is serving — reaching this handler is itself
// proof of that — with the body carrying engined's latest per-daemon heartbeat roll-up
// ({timestamp_ms, daemons:[{name,status,latency_ms}]}) so the Home page can render a status grid.
// Until the first heartbeat round lands, the daemon list is empty rather than stale. Public route.
void WebService::handleHealth(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    (void)req;
    auto& hb = sm.heartbeatService();
    if (hb.hasData())
        fill(resp, 200, hb.latestJson());
    else
        fill(resp, 200, R"({"timestamp_ms":0,"daemons":[]})");
}

void WebService::handleStatic(MgmtdServiceManager& sm, const Request& req, Response& resp)
{
    const auto& cache = sm.staticCache();
    if (!cache)
        return fill(resp, 503, "static cache unavailable", "text/plain; charset=utf-8");

    std::string staticPath(req.target);
    const auto qpos = staticPath.find('?');
    if (qpos != std::string::npos)
        staticPath.erase(qpos);

    auto file = cache->get(staticPath);
    if (!file)
        return fill(resp, 404, "not found", "text/plain; charset=utf-8");

    fill(resp, 200, std::move(file->body), std::move(file->contentType));
    resp.etag = std::move(file->etag);
}

bool WebService::isStaticTarget(const std::string& target)
{
    return target.rfind("/api", 0) != 0;
}

}
