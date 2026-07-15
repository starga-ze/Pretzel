#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

#include <memory>
#include <string>

namespace pz::http
{

class HttpHandler;

using tcp = boost::asio::ip::tcp;

class HttpListener : public std::enable_shared_from_this<HttpListener>
{
public:
    HttpListener(boost::asio::io_context& ioContext, tcp::endpoint endpoint, std::shared_ptr<HttpHandler> handler,
                 std::shared_ptr<boost::asio::ssl::context> sslContext, std::string serverName);

    bool open();
    void run();

private:
    void doAccept();
    void onAccept(boost::system::error_code ec, tcp::socket socket);

private:
    boost::asio::io_context& m_ioContext;
    tcp::endpoint m_endpoint;
    tcp::acceptor m_acceptor;
    std::shared_ptr<HttpHandler> m_handler;
    std::shared_ptr<boost::asio::ssl::context> m_sslContext;
    std::string m_serverName;
};

}
