#pragma once

#include "service/web/WebRouter.h"

#include "http/HttpMessage.h"

#include <string>

namespace pz::mgmtd
{

class MgmtdServiceManager;
class WebEvent;
class WebAction;

// The mgmtd HTTP entry point. It owns the route table and, on each WebEvent, dispatches through it.
// The handlers are static: everything they need comes from the service manager passed in, so they
// hold no state, and the route table is a list of plain function pointers — no std::function, no
// std::bind, no lambdas.
class WebService
{
public:
    WebService() = default;

    void handleEvent(MgmtdServiceManager& serviceManager, const WebEvent& event);

    void handleAction(MgmtdServiceManager& serviceManager, WebAction& action);

private:
    using Request = pz::http::HttpRequest;
    using Response = pz::http::HttpResponse;

    void route(MgmtdServiceManager& sm, const Request& req, Response& resp);

    // Builds the route table on first use, registering the static handlers below.
    void registerRoutes();

    // Non-/api targets that no route claimed: serve the file, redirecting to the login page when the
    // page is not public and the caller has no session.
    static void handleStaticFallback(MgmtdServiceManager& sm, const Request& req, Response& resp);

    // Handlers that stay on the service itself: /metrics is a one-liner over MetricService, and
    // static file serving is the fallback. Every other domain lives in a controller under controller/.
    static void handleMetric(MgmtdServiceManager& sm, const Request& req, Response& resp);
    static void handleStatic(MgmtdServiceManager& sm, const Request& req, Response& resp);

    static bool isStaticTarget(const std::string& target);

    WebRouter m_router;
    bool m_routesRegistered{false};
};

}
