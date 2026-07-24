#include "service/web/WebRouter.h"

#include "service/MgmtdServiceManager.h"

#include <utility>

namespace pz::mgmtd
{

std::string sessionCookie(const pz::http::HttpRequest& req)
{
    const std::string& cookies = req.cookie;
    if (cookies.empty())
        return {};

    const std::string key = "session=";
    auto pos = cookies.find(key);
    while (pos != std::string::npos)
    {
        if (pos == 0 || cookies[pos - 1] == ' ' || cookies[pos - 1] == ';')
            break;
        pos = cookies.find(key, pos + 1);
    }
    if (pos == std::string::npos)
        return {};

    auto end = cookies.find(';', pos);
    if (end == std::string::npos)
        end = cookies.size();

    return cookies.substr(pos + key.size(), end - (pos + key.size()));
}

void WebRouter::add(std::string method, std::string path, Match match, Access access, bool mustChangeExempt,
                    WebHandler handler)
{
    m_routes.push_back(
        Route{std::move(method), std::move(path), match, access, mustChangeExempt, std::move(handler)});
}

void WebRouter::get(std::string path, Access access, WebHandler handler)
{
    add("GET", std::move(path), Match::Exact, access, false, std::move(handler));
}

void WebRouter::post(std::string path, Access access, WebHandler handler)
{
    add("POST", std::move(path), Match::Exact, access, false, std::move(handler));
}

void WebRouter::getPrefix(std::string path, Access access, WebHandler handler)
{
    add("GET", std::move(path), Match::Prefix, access, false, std::move(handler));
}

bool WebRouter::dispatch(MgmtdServiceManager& sm, const pz::http::HttpRequest& req,
                         pz::http::HttpResponse& resp) const
{
    for (const auto& route : m_routes)
    {
        if (route.method != req.method)
            continue;

        const bool hit = (route.match == Match::Exact) ? (req.target == route.path)
                                                       : (req.target.rfind(route.path, 0) == 0);
        if (!hit)
            continue;

        if (route.access == Access::Authenticated)
        {
            if (!sm.authService().validateSession(sessionCookie(req)))
            {
                resp.status = 401;
                resp.contentType = "application/json; charset=utf-8";
                resp.body = R"({"error":"unauthorized"})";
                return true;
            }

            // The must-change-password lock: an authenticated operator with a pending forced change
            // may reach only the change-password route until it is done.
            if (!route.mustChangeExempt && req.target.rfind("/api/", 0) == 0 &&
                sm.authService().mustChangePassword())
            {
                resp.status = 403;
                resp.contentType = "application/json; charset=utf-8";
                resp.body = R"({"error":"password change required","code":"MUST_CHANGE_PASSWORD"})";
                return true;
            }
        }

        route.handler(sm, req, resp);
        return true;
    }

    return false;
}

}
