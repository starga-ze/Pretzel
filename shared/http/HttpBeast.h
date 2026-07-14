#pragma once

#include "http/HttpMessage.h"

#include <boost/beast/http.hpp>

#include <memory>
#include <string>

// Beast <-> transport-agnostic DTO translation, shared by HttpSession and HttpsSession so
// the mapping (and the conditional-GET / ETag rules) live in exactly one place. Everything
// above the session (handler, router, event, service) works on the DTOs and never sees
// beast.
namespace pz::http::detail
{

namespace beast = boost::beast;

using BeastRequest  = beast::http::request<beast::http::string_body>;
using BeastResponse = beast::http::response<beast::http::string_body>;

inline HttpRequest toRequest(const BeastRequest& req)
{
    HttpRequest out;
    out.method = std::string(req.method_string());
    out.target = std::string(req.target());
    if (auto it = req.find(beast::http::field::authorization); it != req.end())
        out.authorization = std::string(it->value());
    if (auto it = req.find(beast::http::field::cookie); it != req.end())
        out.cookie = std::string(it->value());
    out.body = req.body();
    return out;
}

// Build the beast response from the DTO + the originating request. Returned via shared_ptr
// so it outlives the async write. `close` receives need_eof() for the write completion.
inline std::shared_ptr<BeastResponse> toBeastResponse(const BeastRequest& req,
                                                      HttpResponse       out,
                                                      const std::string& serverName,
                                                      bool&              close)
{
    namespace http = beast::http;

    // Conditional GET: an ETag'd response (static assets) whose validator the client
    // already holds collapses to a bodyless 304. ETag'd responses are always-revalidate
    // (Cache-Control: no-cache) — cheap recheck, fresh content.
    bool notModified = false;
    if (!out.etag.empty())
    {
        if (auto it = req.find(http::field::if_none_match); it != req.end())
            notModified = (std::string(it->value()) == out.etag);
    }

    const http::status status = notModified
        ? http::status::not_modified
        : static_cast<http::status>(out.status);

    auto res = std::make_shared<BeastResponse>(status, req.version());
    res->set(http::field::server, serverName);
    if (!out.etag.empty())
    {
        res->set(http::field::etag, out.etag);
        res->set(http::field::cache_control, "no-cache");
    }
    if (!notModified)
    {
        res->set(http::field::content_type, out.contentType);
        if (!out.setCookie.empty())
            res->set(http::field::set_cookie, out.setCookie);
        if (!out.location.empty())
            res->set(http::field::location, out.location);
        if (!out.contentDisposition.empty())
            res->set(http::field::content_disposition, out.contentDisposition);
        res->body() = std::move(out.body);
    }
    res->keep_alive(req.keep_alive());
    res->prepare_payload();

    close = res->need_eof();
    return res;
}

} // namespace pz::http::detail
