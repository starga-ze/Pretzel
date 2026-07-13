#pragma once

#include "http/HttpHandler.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include <memory>

namespace pz::http
{

namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

// Plaintext HTTP/1.1 connection: read one request, hand it to the HttpHandler, write the
// response, repeat until close. Transport only — knows nothing of the daemon's routes.
class HttpSession : public std::enable_shared_from_this<HttpSession>
{
public:
    HttpSession(tcp::socket socket, std::shared_ptr<HttpHandler> handler);

    void run();

private:
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytesTransferred);
    void onWrite(bool close, beast::error_code ec, std::size_t bytesTransferred);
    void doClose();

private:
    tcp::socket m_socket;
    beast::flat_buffer m_buffer;
    beast::http::request<beast::http::string_body> m_request;
    std::shared_ptr<HttpHandler> m_handler;
    std::shared_ptr<void> m_responseHolder;
};

} // namespace pz::http
