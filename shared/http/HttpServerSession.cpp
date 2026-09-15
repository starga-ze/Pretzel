#include "http/HttpServerSession.h"

#include "http/HttpBeast.h"
#include "http/HttpHandler.h"
#include "util/Logger.h"

namespace pz::http
{

HttpServerSession::HttpServerSession(tcp::socket socket, HttpHandler* handler, std::string serverName)
    : m_socket(std::move(socket)), m_handler(handler), m_serverName(std::move(serverName))
{
}

void HttpServerSession::run()
{
    m_handler->addSession(shared_from_this());
    doRead();
}

void HttpServerSession::doRead()
{
    m_request = {};

    beast::http::async_read(m_socket, m_buffer, m_request,
                            beast::bind_front_handler(&HttpServerSession::onRead, shared_from_this()));
}

void HttpServerSession::onRead(beast::error_code ec, std::size_t)
{
    if (ec == beast::http::error::end_of_stream)
    {
        doClose();
        return;
    }

    if (ec)
    {
        LOG_TRACE("HTTP read failed (error={})", ec.message());
        m_handler->removeSession(m_id);
        return;
    }

    m_handler->ingress(detail::toRequest(m_request), m_id);
}

void HttpServerSession::send(HttpResponse response)
{
    bool close = false;
    auto res = detail::toBeastResponse(m_request, std::move(response), m_serverName, close);

    m_responseHolder = res;

    beast::http::async_write(m_socket, *res,
                             beast::bind_front_handler(&HttpServerSession::onWrite, shared_from_this(), close));
}

void HttpServerSession::onWrite(bool close, beast::error_code ec, std::size_t)
{
    if (ec)
    {
        LOG_TRACE("HTTP write failed (error={})", ec.message());
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

void HttpServerSession::doClose()
{
    beast::error_code ec;
    m_socket.shutdown(tcp::socket::shutdown_send, ec);
    m_handler->removeSession(m_id);
}

}
