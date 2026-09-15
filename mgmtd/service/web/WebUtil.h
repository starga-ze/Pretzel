#pragma once

#include "http/HttpMessage.h"
#include "http/UrlEncode.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>
#include <utility>

namespace pz::mgmtd
{

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
            return pz::http::urlDecode(token.substr(eq + 1));
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

// Read a request body that must be a JSON object, or answer 400 and say so.
//
// Every handler that takes a body was writing this itself, and each copy stopped one step short:
// it checked that the body PARSED, not that it was an object. A body of `[]`, `42` or `null`
// parses perfectly and then throws out of the first `input.value(...)` — nlohmann's value() is an
// object accessor — and there is no try/catch between here and the socket, so a well-formed but
// wrong-shaped body took the daemon down rather than earning a 400. Requiring the object here is
// what makes that a refusal instead.
//
// Returns false when it has already filled `resp`; the caller's whole error path is `return`.
inline bool parseBody(const pz::http::HttpRequest& req, pz::http::HttpResponse& resp, nlohmann::json& out)
{
    out = nlohmann::json::parse(req.body, nullptr, /*allow_exceptions=*/false);
    if (out.is_discarded())
    {
        fill(resp, 400, R"({"error":"invalid JSON body"})");
        return false;
    }
    if (!out.is_object())
    {
        fill(resp, 400, R"({"error":"request body must be a JSON object"})");
        return false;
    }
    return true;
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
