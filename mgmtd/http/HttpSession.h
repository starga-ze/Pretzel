#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include <memory>

namespace nf::mgmtd
{

class HttpRouter;

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

class HttpSession : public std::enable_shared_from_this<HttpSession>
{
public:
    HttpSession(tcp::socket socket, std::shared_ptr<HttpRouter> router);

    void run();

private:
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytesTransferred);
    void onWrite(bool close, beast::error_code ec, std::size_t bytesTransferred);
    void doClose();

private:
    tcp::socket m_socket;
    beast::flat_buffer m_buffer;
    http::request<http::string_body> m_request;
    std::shared_ptr<HttpRouter> m_router;
    std::shared_ptr<void> m_responseHolder;
};

} // namespace nf::mgmtd

