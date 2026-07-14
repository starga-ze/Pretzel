#pragma once

#include <string>

namespace pz::http
{

// Pretzel's transport-agnostic HTTP messages: the parsed, beast-free forms of an inbound
// request and its response — the HTTP analogue of pz::ipc::IpcMessage. The session (the only
// beast-aware layer) translates boost::beast <-> these at the edge (see HttpBeast.h);
// everything past it (handler, router, event, action, service) works on these and never sees
// beast, so HTTP flows through the same event/action machinery as IPC.
//
// These OWN their data (they are not views). A request is parked and processed on a later
// tick — after the beast buffer it was parsed from has been reused — so it must carry its
// own bytes, not reference the transport's.

// An inbound HTTP request.
struct HttpRequest
{
    std::string method;         // "GET", "POST", ...
    std::string target;         // "/api/probe/egress"
    std::string authorization;  // raw "Authorization" header value ("" if absent)
    std::string cookie;         // raw "Cookie" header value ("" if absent)
    std::string body;           // raw request body
};

// The response to an HttpRequest.

struct HttpResponse
{
    int         status{404};
    std::string contentType{"application/json; charset=utf-8"};
    std::string body{R"({"error":"not found"})"};

    // Optional response headers; the Handler emits each only when non-empty. Kept as
    // explicit named fields (like the request's authorization/cookie) rather than a
    // generic map, since the daemons need exactly these.
    std::string setCookie;          // "Set-Cookie" value (session login/logout, SSO)
    std::string location;           // "Location" value (302 redirects)
    std::string contentDisposition; // "Content-Disposition" value (file attachments)
    std::string etag;               // "ETag" value; when set, the Handler emits it plus
                                    // Cache-Control and answers matching If-None-Match with
                                    // 304 (conditional GET for cacheable static assets).
};

} // namespace pz::http
