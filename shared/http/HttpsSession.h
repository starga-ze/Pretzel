#pragma once

#include "http/HttpHandler.h"
#include "http/HttpResponder.h"

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

// TLS variant of HttpSession: perform the handshake, then the same asynchronous
// read → handle → (later) send loop over the encrypted stream. Transport only.
class HttpsSession : public std::enable_shared_from_this<HttpsSession>,
                     public HttpResponder
{
public:
    HttpsSession(tcp::socket socket,
                 std::shared_ptr<HttpHandler> handler,
                 std::shared_ptr<boost::asio::ssl::context> sslContext,
                 std::string serverName);

    void run();

    // HttpResponder: deliver the response for the in-flight request (async write).
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
    std::shared_ptr<HttpHandler> m_handler;
    std::string m_serverName;
    std::shared_ptr<void> m_responseHolder;
};

} // namespace pz::http
