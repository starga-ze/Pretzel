#pragma once

#include "http/HttpSessionBase.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include <memory>
#include <string>

namespace pz::http
{

namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

class HttpHandler;

// Plaintext HTTP/1.1 connection. Transport only — knows nothing of the daemon's routes.
//
// The cycle is asynchronous: read one request, hand it (as a DTO, tagged with this session's
// SessionId) to the handler, then return. The handler posts an event; the response arrives
// later via send(), which writes it and reads the next request. The session keeps itself
// alive across that gap by registering in the handler (which holds a shared_ptr to it until
// the connection closes). One request is in flight at a time, so ordering is preserved.
class HttpSession : public std::enable_shared_from_this<HttpSession>,
                    public HttpSessionBase
{
public:
    HttpSession(tcp::socket socket,
                HttpHandler* handler,
                std::string serverName);

    void run();

    // HttpSessionBase: deliver the response for the in-flight request (async write).
    void send(HttpResponse response) override;

private:
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytesTransferred);
    void onWrite(bool close, beast::error_code ec, std::size_t bytesTransferred);
    void doClose();

private:
    tcp::socket m_socket;
    beast::flat_buffer m_buffer;
    beast::http::request<beast::http::string_body> m_request;
    HttpHandler* m_handler;
    std::string m_serverName;
    std::shared_ptr<void> m_responseHolder;
};

} // namespace pz::http
