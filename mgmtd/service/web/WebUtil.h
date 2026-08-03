#pragma once

#include "http/HttpMessage.h"

#include <string>
#include <utility>

namespace pz::mgmtd
{

// Fill an HTTP response in one line — the small shape every web handler ends on. Inline in a header
// so each controller shares the one definition rather than repeating it.
inline void fill(pz::http::HttpResponse& r, int status, std::string body,
                 std::string contentType = "application/json; charset=utf-8")
{
    r.status = status;
    r.contentType = std::move(contentType);
    r.body = std::move(body);
}

// The session cookie value, or empty. Shared by the router's auth gate and the handlers that read
// the session (logout, whoami); inline here so there is one parser and no separate translation unit.
inline std::string sessionCookie(const pz::http::HttpRequest& req)
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

}
