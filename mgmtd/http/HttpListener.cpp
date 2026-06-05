#include "http/HttpListener.h"

#include "http/HttpSession.h"
#include "http/HttpsSession.h"
#include "util/Logger.h"

#include <functional>

namespace nf::mgmtd
{

HttpListener::HttpListener(boost::asio::io_context& ioContext,
                           tcp::endpoint endpoint,
                           std::shared_ptr<HttpRouter> router,
                           std::shared_ptr<boost::asio::ssl::context> sslContext)
    : m_ioContext(ioContext),
      m_endpoint(endpoint),
      m_acceptor(ioContext),
      m_router(std::move(router)),
      m_sslContext(std::move(sslContext))
{
}

bool HttpListener::open()
{
    boost::system::error_code ec;

    m_acceptor.open(m_endpoint.protocol(), ec);
    if (ec)
    {
        LOG_ERROR("Mgmtd HTTP acceptor open failed: {}", ec.message());
        return false;
    }

    m_acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec)
    {
        LOG_ERROR("Mgmtd HTTP reuse_address failed: {}", ec.message());
        return false;
    }

    m_acceptor.bind(m_endpoint, ec);
    if (ec)
    {
        LOG_ERROR("Mgmtd HTTP bind failed: {}", ec.message());
        return false;
    }

    m_acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        LOG_ERROR("Mgmtd HTTP listen failed: {}", ec.message());
        return false;
    }

    if (m_sslContext)
    {
        LOG_INFO("Mgmtd HTTPS listener opened {}:{}",
                 m_endpoint.address().to_string(),
                 m_endpoint.port());
    }
    else
    {
        LOG_INFO("Mgmtd HTTP listener opened {}:{}",
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
                                           m_router,
                                           m_sslContext)->run();
        }
        else
        {
            std::make_shared<HttpSession>(std::move(socket), m_router)->run();
        }
    }
    else
    {
        LOG_DEBUG("Mgmtd HTTP accept failed: {}", ec.message());
    }

    doAccept();
}

} // namespace nf::mgmtd
