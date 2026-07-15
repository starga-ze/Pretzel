#pragma once

#include "http/HttpSessionBase.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include <memory>
#include <string>

namespace pz::http
{

namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

class HttpHandler;

// TLS variant of HttpSession: perform the handshake, then the same asynchronous
// read -> ingress -> (later) send loop over the encrypted stream. Transport only.
class HttpsSession : public std::enable_shared_from_this<HttpsSession>,
                     public HttpSessionBase
{
public:
    HttpsSession(tcp::socket socket,
                 HttpHandler* handler,
                 std::shared_ptr<boost::asio::ssl::context> sslContext,
                 std::string serverName);

    void run();

    // HttpSessionBase: deliver the response for the in-flight request (async write).
    void send(HttpResponse response) override;

private:
    void doHandshake();
    void onHandshake(beast::error_code ec);

    void doRead();
    void onRead(beast::error_code ec, std::size_t bytesTransferred);

    void onWrite(bool close, beast::error_code ec, std::size_t bytesTransferred);

    void doClose();
    void onShutdown(beast::error_code ec);

private:
    boost::asio::ssl::stream<tcp::socket> m_stream;
    beast::flat_buffer m_buffer;
    beast::http::request<beast::http::string_body> m_request;
    HttpHandler* m_handler;
    std::string m_serverName;
    std::shared_ptr<void> m_responseHolder;
};

} // namespace pz::http
