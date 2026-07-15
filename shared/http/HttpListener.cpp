#include "http/HttpListener.h"

#include "http/HttpHandler.h"
#include "http/HttpSession.h"
#include "http/HttpsSession.h"
#include "util/Logger.h"

#include <functional>

namespace pz::http
{

HttpListener::HttpListener(boost::asio::io_context& ioContext,
                           tcp::endpoint endpoint,
                           std::shared_ptr<HttpHandler> handler,
                           std::shared_ptr<boost::asio::ssl::context> sslContext,
                           std::string serverName)
    : m_ioContext(ioContext),
      m_endpoint(endpoint),
      m_acceptor(ioContext),
      m_handler(std::move(handler)),
      m_sslContext(std::move(sslContext)),
      m_serverName(std::move(serverName))
{
}

bool HttpListener::open()
{
    boost::system::error_code ec;

    m_acceptor.open(m_endpoint.protocol(), ec);
    if (ec)
    {
        LOG_ERROR("acceptor open failed (error={})", ec.message());
        return false;
    }

    m_acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec)
    {
        LOG_ERROR("reuse_address failed (error={})", ec.message());
        return false;
    }

    m_acceptor.bind(m_endpoint, ec);
    if (ec)
    {
        LOG_ERROR("bind failed (error={})", ec.message());
        return false;
    }

    m_acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        LOG_ERROR("listen failed (error={})", ec.message());
        return false;
    }

    if (m_sslContext)
    {
        LOG_INFO("HTTPS listener opened (address={}, port={})",
                 m_endpoint.address().to_string(),
                 m_endpoint.port());
    }
    else
    {
        LOG_INFO("HTTP listener opened (address={}, port={})",
                 m_endpoint.address().to_string(),
                 m_endpoint.port());
    }

    return true;
}

void HttpListener::run()
{
    doAccept();
}

void HttpListener::doAccept()
{
    m_acceptor.async_accept(std::bind(&HttpListener::onAccept,
                                      shared_from_this(),
                                      std::placeholders::_1,
                                      std::placeholders::_2));
}

void HttpListener::onAccept(boost::system::error_code ec, tcp::socket socket)
{
    if (!ec)
    {
        if (m_sslContext)
        {
            std::make_shared<HttpsSession>(std::move(socket),
                                           m_handler.get(),
                                           m_sslContext,
                                           m_serverName)->run();
        }
        else
        {
            std::make_shared<HttpSession>(std::move(socket),
                                          m_handler.get(),
                                          m_serverName)->run();
        }
    }
    else
    {
        LOG_TRACE("accept failed (error={})", ec.message());
    }

    doAccept();
}

} // namespace pz::http
