#include "http/HttpClient.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cctype>
#include <memory>

namespace pz::http
{

namespace
{

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

std::string sha256Fingerprint(X509* cert)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (!cert || X509_digest(cert, EVP_sha256(), md, &len) != 1)
        return {};

    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 3);
    for (unsigned int i = 0; i < len; ++i)
    {
        if (i)
            out.push_back(':');
        out.push_back(hex[(md[i] >> 4) & 0xF]);
        out.push_back(hex[md[i] & 0xF]);
    }
    return out;
}

std::string subjectLine(X509* cert)
{
    if (!cert)
        return {};
    char buf[512] = {0};
    X509_NAME_oneline(X509_get_subject_name(cert), buf, sizeof(buf) - 1);
    return buf;
}

// Compare fingerprints ignoring case and separators, so a pin pasted as "aabb..." still
// matches one captured as "AA:BB:..".
bool sameFingerprint(const std::string& a, const std::string& b)
{
    auto canon = [](const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (std::isxdigit(static_cast<unsigned char>(c)))
                out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        return out;
    };
    const std::string ca = canon(a);
    const std::string cb = canon(b);
    return !ca.empty() && ca == cb;
}

}

std::string urlEncode(const std::string& raw)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size() * 3);
    for (unsigned char c : raw)
    {
        const bool unreserved = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved)
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

namespace
{

// One outbound exchange. The phases were already a callback chain when this ran on a private
// io_context; making it usable from a daemon's shared context only meant moving the state off
// the stack — the session holds itself alive through shared_from_this until it settles, so
// there is no run() for the frame to block on.
class ClientSession : public std::enable_shared_from_this<ClientSession>
{
public:
    ClientSession(net::io_context& ioc, ClientRequest req, ResponseHandler onDone)
        : m_req(std::move(req)),
          m_onDone(std::move(onDone)),
          m_sslCtx(ssl::context::tls_client),
          m_resolver(ioc),
          m_stream(ioc, m_sslCtx)
    {
    }

    // Never throws: a failure during setup still has to reach the handler, or the caller's
    // ticket stays pending forever.
    void run()
    {
        try
        {
            start();
        }
        catch (const std::exception& e)
        {
            m_out.error = e.what();
            finish();
        }
    }

private:
    void start()
    {
        m_sslCtx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                             ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
        if (m_req.verifyCa)
            m_sslCtx.set_default_verify_paths();

        // SNI — some platforms serve a different certificate without it.
        if (!SSL_set_tlsext_host_name(m_stream.native_handle(), m_req.host.c_str()))
        {
            m_out.error = "failed to set TLS SNI hostname";
            return finish();
        }

        if (m_req.verifyCa)
        {
            m_stream.set_verify_mode(ssl::verify_peer);
            m_stream.set_verify_callback(ssl::host_name_verification(m_req.host));
        }
        else
        {
            // Pinning replaces chain verification; the fingerprint is checked in onHandshake,
            // before anything is written.
            m_stream.set_verify_mode(ssl::verify_none);
        }

        m_httpReq = {http::string_to_verb(m_req.method), m_req.target, 11};
        m_httpReq.set(http::field::host, m_req.host);
        m_httpReq.set(http::field::user_agent, "pretzel");
        for (const auto& [name, value] : m_req.headers)
            m_httpReq.set(name, value);
        if (!m_req.body.empty())
        {
            m_httpReq.body() = m_req.body;
            m_httpReq.prepare_payload();
        }

        m_stage = "resolve";
        arm();
        m_resolver.async_resolve(m_req.host, std::to_string(m_req.port),
                                 [self = shared_from_this()](beast::error_code ec, tcp::resolver::results_type results)
                                 { self->onResolve(ec, std::move(results)); });
    }

    // Each phase re-arms the deadline, so the timeout bounds every step rather than only the
    // first one. On expiry beast cancels the pending operation and the handler reports it.
    void arm()
    {
        beast::get_lowest_layer(m_stream).expires_after(m_req.timeout);
    }

    void fail(beast::error_code ec)
    {
        m_out.error = m_stage + ": " + ec.message();
        finish();
    }

    // The single exit. Every path lands here exactly once, and the socket is torn down before
    // the caller's handler runs so a handler that starts another exchange cannot collide with
    // this one's shutdown.
    void finish()
    {
        if (m_done)
            return;
        m_done = true;

        beast::error_code ignored;
        beast::get_lowest_layer(m_stream).socket().shutdown(tcp::socket::shutdown_both, ignored);

        if (m_onDone)
            m_onDone(std::move(m_out));
    }

    void onResolve(beast::error_code ec, tcp::resolver::results_type results)
    {
        if (ec)
            return fail(ec);

        m_stage = "connect";
        arm();
        beast::get_lowest_layer(m_stream).async_connect(
            results, [self = shared_from_this()](beast::error_code ec2, const tcp::endpoint&) { self->onConnect(ec2); });
    }

    void onConnect(beast::error_code ec)
    {
        if (ec)
            return fail(ec);

        m_stage = "handshake";
        arm();
        m_stream.async_handshake(ssl::stream_base::client,
                                 [self = shared_from_this()](beast::error_code ec2) { self->onHandshake(ec2); });
    }

    void onHandshake(beast::error_code ec)
    {
        if (ec)
            return fail(ec);

        m_out.tlsOk = true;

        X509* cert = SSL_get_peer_certificate(m_stream.native_handle());
        m_out.fingerprint = sha256Fingerprint(cert);
        m_out.certSubject = subjectLine(cert);
        if (cert)
            X509_free(cert);

        // ── Pin gate ────────────────────────────────────────────────────────────────────
        // Nothing is written until the peer is known to be the pinned device, so a
        // man-in-the-middle never receives the credential.
        if (m_req.verifyCa)
        {
            m_out.pinMatched = true;   // the CA chain already vouched
        }
        else if (m_req.expectedFingerprint.empty())
        {
            return finish();   // first contact: report the fingerprint, send nothing
        }
        else if (!sameFingerprint(m_req.expectedFingerprint, m_out.fingerprint))
        {
            return finish();   // pin mismatch: send nothing
        }
        else
        {
            m_out.pinMatched = true;
        }

        m_stage = "write";
        m_out.requestSent = true;
        arm();
        http::async_write(m_stream, m_httpReq,
                          [self = shared_from_this()](beast::error_code ec2, std::size_t) { self->onWrite(ec2); });
    }

    void onWrite(beast::error_code ec)
    {
        if (ec)
            return fail(ec);

        m_stage = "read";
        arm();
        http::async_read(m_stream, m_buffer, m_httpRes,
                         [self = shared_from_this()](beast::error_code ec2, std::size_t) { self->onRead(ec2); });
    }

    void onRead(beast::error_code ec)
    {
        if (ec)
            return fail(ec);

        m_out.status = static_cast<int>(m_httpRes.result_int());
        m_out.body = m_httpRes.body();
        finish();
    }

    ClientRequest m_req;
    ResponseHandler m_onDone;

    // Declared before m_stream: the stream holds a reference to this context.
    ssl::context m_sslCtx;
    tcp::resolver m_resolver;
    beast::ssl_stream<beast::tcp_stream> m_stream;

    beast::flat_buffer m_buffer;
    http::request<http::string_body> m_httpReq;
    http::response<http::string_body> m_httpRes;

    ClientResponse m_out;
    std::string m_stage{"init"};
    bool m_done{false};
};

}

void requestAsync(net::io_context& ioc, ClientRequest req, ResponseHandler onDone)
{
    try
    {
        // onDone is copied rather than moved: run() cannot throw, so reaching the catch means
        // the session was never constructed and never took ownership of the handler.
        std::make_shared<ClientSession>(ioc, std::move(req), onDone)->run();
    }
    catch (const std::exception& e)
    {
        ClientResponse out;
        out.error = e.what();
        if (onDone)
            onDone(std::move(out));
    }
}

}
