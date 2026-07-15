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

class HttpSession : public std::enable_shared_from_this<HttpSession>, public HttpSessionBase
{
public:
    HttpSession(tcp::socket socket, HttpHandler* handler, std::string serverName);

    void run();

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

}
