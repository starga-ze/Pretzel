#pragma once

#include "http/HttpClient.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <memory>
#include <string>

namespace pz::http
{

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

// One outbound HTTPS exchange, structured to mirror the inbound HttpSession: a self-owning session
// whose phases are named member functions wired with beast::bind_front_handler — no lambdas. Each
// async step re-arms the deadline and forwards to the next on_* member; a single finish() is the
// only exit.
//
// resolve → connect → handshake → [pin gate] → write → read → finish
//
// The pin is checked in onHandshake, before anything is written, so a man-in-the-middle never
// receives the credential. The session holds itself alive through shared_from_this until it
// settles, so there is no run loop for the caller's frame to block on.
class ClientSession : public std::enable_shared_from_this<ClientSession>
{
public:
    ClientSession(net::io_context& ioc, ClientRequest req, ResponseHandler onDone);

    // Never throws: a failure during setup still has to reach the handler, or the caller's ticket
    // stays pending forever.
    void run();

private:
    void start();

    // Re-arm the per-step deadline. Called before each async op so the timeout bounds every phase,
    // not just the first.
    void arm();

    void fail(beast::error_code ec);

    // The single exit. Every path lands here exactly once, and the socket is torn down before the
    // caller's handler runs so a handler that starts another exchange cannot collide with this
    // one's shutdown.
    void finish();

    void onResolve(beast::error_code ec, tcp::resolver::results_type results);
    void onConnect(beast::error_code ec, const tcp::endpoint& endpoint);
    void onHandshake(beast::error_code ec);
    void onWrite(beast::error_code ec, std::size_t bytesTransferred);
    void onRead(beast::error_code ec, std::size_t bytesTransferred);

    ClientRequest m_req;
    ResponseHandler m_onDone;

    // Declared before m_stream: the stream holds a reference to this context.
    ssl::context m_sslCtx;
    tcp::resolver m_resolver;
    beast::ssl_stream<beast::tcp_stream> m_stream;

    beast::flat_buffer m_buffer;
    beast::http::request<beast::http::string_body> m_httpReq;
    beast::http::response<beast::http::string_body> m_httpRes;

    ClientResponse m_out;
    std::string m_stage{"init"};
    bool m_done{false};
};

}
