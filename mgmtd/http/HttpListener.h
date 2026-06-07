#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

#include <memory>

namespace pz::mgmtd
{

class HttpRouter;

using tcp = boost::asio::ip::tcp;

class HttpListener : public std::enable_shared_from_this<HttpListener>
{
public:
    HttpListener(boost::asio::io_context& ioContext,
                 tcp::endpoint endpoint,
                 std::shared_ptr<HttpRouter> router,
                 std::shared_ptr<boost::asio::ssl::context> sslContext);

    bool open();
    void run();

private:
    void doAccept();
    void onAccept(boost::system::error_code ec, tcp::socket socket);

private:
    boost::asio::io_context& m_ioContext;
    tcp::endpoint m_endpoint;
    tcp::acceptor m_acceptor;
    std::shared_ptr<HttpRouter> m_router;
    std::shared_ptr<boost::asio::ssl::context> m_sslContext;
};

} // namespace pz::mgmtd
