#include "http/HttpsSession.h"

#include "http/HttpRouter.h"
#include "util/Logger.h"

#include <boost/asio/ssl.hpp>

namespace pz::mgmtd
{

HttpsSession::HttpsSession(tcp::socket socket,
                           std::shared_ptr<HttpRouter> router,
                           std::shared_ptr<boost::asio::ssl::context> sslContext)
    : m_stream(std::move(socket), *sslContext),
      m_router(std::move(router))
{
}

void HttpsSession::run()
{
    doHandshake();
}

void HttpsSession::doHandshake()
{
    m_stream.async_handshake(boost::asio::ssl::stream_base::server,
                             beast::bind_front_handler(&HttpsSession::onHandshake,
                                                       shared_from_this()));
}

void HttpsSession::onHandshake(beast::error_code ec)
{
    if (ec)
    {
        LOG_TRACE("HTTPS handshake failed (error={})", ec.message());
        return;
    }

    doRead();
}

void HttpsSession::doRead()
{
    m_request = {};

    http::async_read(m_stream,
                     m_buffer,
                     m_request,
                     beast::bind_front_handler(&HttpsSession::onRead,
                                               shared_from_this()));
}

void HttpsSession::onRead(beast::error_code ec, std::size_t)
{
    if (ec == http::error::end_of_stream)
    {
        doClose();
        return;
    }

    if (ec)
    {
        LOG_TRACE("HTTPS read failed (error={})", ec.message());
        return;
    }

    auto response = m_router->handle(m_request);
    const bool close = response.need_eof();

    m_responseHolder =
        std::make_shared<http::response<http::string_body>>(std::move(response));

    auto& responseRef =
        *static_cast<http::response<http::string_body>*>(m_responseHolder.get());

    http::async_write(m_stream,
                      responseRef,
                      beast::bind_front_handler(&HttpsSession::onWrite,
                                                shared_from_this(),
                                                close));
}

void HttpsSession::onWrite(bool close, beast::error_code ec, std::size_t)
{
    if (ec)
    {
        LOG_TRACE("HTTPS write failed (error={})", ec.message());
        return;
    }

    if (close)
    {
        doClose();
        return;
    }

    m_responseHolder.reset();
    doRead();
}

void HttpsSession::doClose()
{
    m_stream.async_shutdown(
        beast::bind_front_handler(&HttpsSession::onShutdown,
                                  shared_from_this()));
}

void HttpsSession::onShutdown(beast::error_code ec)
{
    if (ec && ec != boost::asio::error::eof)
    {
        LOG_TRACE("HTTPS shutdown failed (error={})", ec.message());
    }
}

} // namespace pz::mgmtd
