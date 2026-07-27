#pragma once

#include "http/HttpMessage.h"

#include <string>
#include <vector>

namespace pz::mgmtd
{

class MgmtdServiceManager;

// One route's work: fill `resp` for `req`. A plain function pointer — the handlers are static (they
// read everything they need from the service manager), so registration is just `&handleThing`, with
// no std::function, std::bind or lambda in sight. Runs on the service loop (post-event), so it may
// touch service state directly.
using WebHandler = void (*)(MgmtdServiceManager&, const pz::http::HttpRequest&, pz::http::HttpResponse&);

// The session cookie value, or empty. Free function so the router's auth gate and the handlers
// that read the session (logout, whoami) share one parser.
std::string sessionCookie(const pz::http::HttpRequest& req);

// The mgmtd HTTP dispatch table — the "router" seam between a WebEvent and a domain controller.
//
// Controllers register their routes here; dispatch() matches (method, path), applies the two
// cross-cutting gates ONCE — authentication and the must-change-password lock — then runs the
// handler. That is what lets a handler be pure request→response glue: none of them repeat
// `if (!authed) return unauthorized`.
//
// It runs on the service loop, not the transport thread: MgmtdRxRouter only wraps an inbound
// request into a WebEvent (crossing the thread boundary); the URL routing happens here when the
// event is processed. Distinct, therefore, from that transport-facing RxRouter.
class WebRouter
{
public:
    enum class Access
    {
        Public,          // no session required (login, health, SAML ACS, …)
        Authenticated,   // a valid session is required
    };

    enum class Match
    {
        Exact,           // req.target == path
        Prefix,          // req.target starts with path (query string or sub-path follows)
    };

    // Convenience registrars for the common cases (exact match).
    void get(std::string path, Access access, WebHandler handler);
    void post(std::string path, Access access, WebHandler handler);
    void getPrefix(std::string path, Access access, WebHandler handler);

    // The general form. `mustChangeExempt` marks a route that still needs a session but must not be
    // blocked by the must-change-password lock — only /api/change-password, the escape from it.
    void add(std::string method, std::string path, Match match, Access access, bool mustChangeExempt,
             WebHandler handler);

    // Returns true when a route matched — handled, or short-circuited by a gate. Returns false when
    // nothing matched, so the caller can fall back to static file serving.
    bool dispatch(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp) const;

private:
    struct Route
    {
        std::string method;
        std::string path;
        Match match{Match::Exact};
        Access access{Access::Authenticated};
        bool mustChangeExempt{false};
        WebHandler handler;
    };

    std::vector<Route> m_routes;
};

}
