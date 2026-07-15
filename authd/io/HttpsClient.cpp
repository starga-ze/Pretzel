#include "io/HttpsClient.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <chrono>
#include <exception>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = asio::ip::tcp;

namespace pz::net
{

std::string HttpsClient::urlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

HttpsClient::Response HttpsClient::send(const Request& req)
{
    Response out;
    const auto timeout = std::chrono::milliseconds(req.timeoutMs);

    try
    {
        asio::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        if (req.verifyTls)
        {
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_peer);
        }
        else
        {
            ctx.set_verify_mode(ssl::verify_none);
        }

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), req.host.c_str()))
        {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        const auto results = resolver.resolve(req.host, std::to_string(req.port));

        beast::get_lowest_layer(stream).expires_after(timeout);
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        const http::verb verb = (req.method == "POST") ? http::verb::post : http::verb::get;

        http::request<http::string_body> hreq{verb, req.target, 11};
        hreq.set(http::field::host, req.host);
        hreq.set(http::field::user_agent, "pretzel-authd/1.0");
        hreq.set(http::field::accept, "application/json");
        if (!req.body.empty())
        {
            hreq.set(http::field::content_type, req.contentType);
            hreq.body() = req.body;
            hreq.prepare_payload();
        }

        beast::get_lowest_layer(stream).expires_after(timeout);
        http::write(stream, hreq);

        beast::flat_buffer buffer;
        http::response<http::string_body> hres;
        http::read(stream, buffer, hres);

        out.status = hres.result_int();
        out.body = std::move(hres.body());
        out.ok = (out.status >= 200 && out.status < 300);

        beast::error_code ec;
        stream.shutdown(ec);
    }
    catch (const std::exception& e)
    {
        out.ok = false;
        out.error = e.what();
    }

    return out;
}

HttpsClient::Response HttpsClient::get(const std::string& host, uint16_t port, const std::string& target, int timeoutMs,
                                       bool verifyTls)
{
    Request req;
    req.method = "GET";
    req.host = host;
    req.port = port;
    req.target = target;
    req.timeoutMs = timeoutMs;
    req.verifyTls = verifyTls;
    return send(req);
}

HttpsClient::Response HttpsClient::postForm(const std::string& host, uint16_t port, const std::string& target,
                                            const std::string& formBody, int timeoutMs, bool verifyTls)
{
    Request req;
    req.method = "POST";
    req.host = host;
    req.port = port;
    req.target = target;
    req.body = formBody;
    req.timeoutMs = timeoutMs;
    req.verifyTls = verifyTls;
    return send(req);
}

}
