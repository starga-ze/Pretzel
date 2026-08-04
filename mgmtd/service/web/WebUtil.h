#pragma once

#include "http/HttpMessage.h"

#include <cctype>
#include <sstream>
#include <string>
#include <utility>

namespace pz::mgmtd
{

// Percent-decode a query value ('+' is a space), so a filter term can carry spaces and punctuation.
inline std::string urlDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == '+')
        {
            out.push_back(' ');
        }
        else if (in[i] == '%' && i + 2 < in.size() && std::isxdigit((unsigned char)in[i + 1]) &&
                 std::isxdigit((unsigned char)in[i + 2]))
        {
            out.push_back(static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        }
        else
        {
            out.push_back(in[i]);
        }
    }
    return out;
}

// One query-string value from a request target, decoded; empty when absent. Every GET route that
// takes filters parses its target this way, so the parser lives here rather than once per controller.
inline std::string queryParam(const std::string& target, const std::string& key)
{
    const auto q = target.find('?');
    if (q == std::string::npos)
        return {};

    std::istringstream ss(target.substr(q + 1));
    std::string token;
    while (std::getline(ss, token, '&'))
    {
        const auto eq = token.find('=');
        if (eq != std::string::npos && token.substr(0, eq) == key)
            return urlDecode(token.substr(eq + 1));
    }
    return {};
}

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
