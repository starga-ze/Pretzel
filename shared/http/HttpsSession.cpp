#include "http/HttpsSession.h"

#include "http/HttpBeast.h"
#include "http/HttpHandler.h"
#include "util/Logger.h"

#include <boost/asio/ssl.hpp>

namespace pz::http
{

HttpsSession::HttpsSession(tcp::socket socket,
                           HttpHandler* handler,
                           std::shared_ptr<boost::asio::ssl::context> sslContext,
                           std::string serverName)
    : m_stream(std::move(socket), *sslContext),
      m_handler(handler),
      m_serverName(std::move(serverName))
{
}

void HttpsSession::run()
{
    // Register with the handler: stamps our SessionId and keeps us alive across the handshake
    // and every subsequent parked request (see HttpSession::run).
    m_handler->addSession(shared_from_this());
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
        m_handler->removeSession(m_id);
        return;
    }

    doRead();
}

void HttpsSession::doRead()
{
    m_request = {};

    beast::http::async_read(m_stream,
                            m_buffer,
                            m_request,
                            beast::bind_front_handler(&HttpsSession::onRead,
                                                      shared_from_this()));
}

void HttpsSession::onRead(beast::error_code ec, std::size_t)
{
    LOG_INFO("HttpsSession onRead call");

    if (ec == beast::http::error::end_of_stream)
    {
        doClose();
        return;
    }

    if (ec)
    {
        LOG_TRACE("HTTPS read failed (error={})", ec.message());
        m_handler->removeSession(m_id);
        return;
    }

    // Ingress: hand a transport-agnostic request tagged with our SessionId to the handler. It
    // posts an event and returns; the response arrives later via send().
    m_handler->ingress(detail::toRequest(m_request), m_id);
}

void HttpsSession::send(HttpResponse response)
{
    bool close = false;
    auto res = detail::toBeastResponse(m_request, std::move(response), m_serverName, close);

    m_responseHolder = res;  // keep the response alive for the duration of the write

    beast::http::async_write(m_stream,
                             *res,
                             beast::bind_front_handler(&HttpsSession::onWrite,
                                                       shared_from_this(),
                                                       close));
}

void HttpsSession::onWrite(bool close, beast::error_code ec, std::size_t)
{
    if (ec)
    {
        LOG_TRACE("HTTPS write failed (error={})", ec.message());
        m_handler->removeSession(m_id);
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

    m_handler->removeSession(m_id);
}

} // namespace pz::http
