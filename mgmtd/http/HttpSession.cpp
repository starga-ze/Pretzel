#include "http/HttpSession.h"

#include "http/HttpRouter.h"
#include "util/Logger.h"

namespace pz::mgmtd
{

HttpSession::HttpSession(tcp::socket socket, std::shared_ptr<HttpRouter> router)
    : m_socket(std::move(socket)),
      m_router(std::move(router))
{
}

void HttpSession::run()
{
    doRead();
}

void HttpSession::doRead()
{
    m_request = {};

    http::async_read(m_socket,
                     m_buffer,
                     m_request,
                     beast::bind_front_handler(&HttpSession::onRead,
                                               shared_from_this()));
}

void HttpSession::onRead(beast::error_code ec, std::size_t)
{
    if (ec == http::error::end_of_stream)
    {
        doClose();
        return;
    }

    if (ec)
    {
        LOG_TRACE("HTTP read failed (error={})", ec.message());
        return;
    }

    auto response = m_router->handle(m_request);
    const bool close = response.need_eof();

    m_responseHolder = std::make_shared<http::response<http::string_body>>(std::move(response));
    auto& responseRef = *static_cast<http::response<http::string_body>*>(m_responseHolder.get());

    http::async_write(m_socket,
                      responseRef,
                      beast::bind_front_handler(&HttpSession::onWrite,
                                                shared_from_this(),
                                                close));
}

void HttpSession::onWrite(bool close, beast::error_code ec, std::size_t)
{
    if (ec)
    {
        LOG_TRACE("HTTP write failed (error={})", ec.message());
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

void HttpSession::doClose()
{
    beast::error_code ec;
    m_socket.shutdown(tcp::socket::shutdown_send, ec);
}

} // namespace pz::mgmtd

