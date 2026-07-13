#pragma once

#include <boost/beast/http.hpp>

namespace pz::http
{

namespace beast = boost::beast;

// Transport-facing contract for the HTTP server, mirroring pz::ipc::IpcHandler for the
// IPC transport. The listener/session layer depends only on this base — never on a
// concrete router — so each daemon (mgmtd, apid, authd, ...) plugs in its own handler
// that turns a parsed request into a response.
//
// handle() runs on the server's single io_context poll thread and must NOT block (e.g.
// on an IPC round-trip); asynchronous flows are split across requests (the client polls
// a result endpoint) — see mgmtd's SAML handlers for the pattern.
class HttpHandler
{
public:
    using Request  = beast::http::request<beast::http::string_body>;
    using Response = beast::http::response<beast::http::string_body>;

    HttpHandler() = default;
    virtual ~HttpHandler() = default;

    virtual Response handle(const Request& req) = 0;
};

} // namespace pz::http
