#include "client/HttpClientSession.h"

#include <boost/asio/connect.hpp>

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <cctype>
#include <string>
#include <utility>

namespace pz::http
{

namespace
{

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

// Compare fingerprints ignoring case and separators, so a pin pasted as "aabb..." still matches
// one captured as "AA:BB:..".
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

ClientSession::ClientSession(net::io_context& ioc, ClientRequest req, ResponseHandler onDone)
    : m_req(std::move(req)),
      m_onDone(std::move(onDone)),
      m_sslCtx(ssl::context::tls_client),
      m_resolver(ioc),
      m_stream(ioc, m_sslCtx)
{
}

void ClientSession::run()
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

void ClientSession::start()
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
        // Pinning replaces chain verification; the fingerprint is checked in onHandshake, before
        // anything is written.
        m_stream.set_verify_mode(ssl::verify_none);
    }

    m_httpReq = {beast::http::string_to_verb(m_req.method), m_req.target, 11};
    m_httpReq.set(beast::http::field::host, m_req.host);
    m_httpReq.set(beast::http::field::user_agent, "pretzel");
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
                             beast::bind_front_handler(&ClientSession::onResolve, shared_from_this()));
}

void ClientSession::arm()
{
    beast::get_lowest_layer(m_stream).expires_after(m_req.timeout);
}

void ClientSession::fail(beast::error_code ec)
{
    m_out.error = m_stage + ": " + ec.message();
    finish();
}

void ClientSession::finish()
{
    if (m_done)
        return;
    m_done = true;

    beast::error_code ignored;
    beast::get_lowest_layer(m_stream).socket().shutdown(tcp::socket::shutdown_both, ignored);

    if (m_onDone)
        m_onDone(std::move(m_out));
}

void ClientSession::onResolve(beast::error_code ec, tcp::resolver::results_type results)
{
    if (ec)
        return fail(ec);

    m_stage = "connect";
    arm();
    beast::get_lowest_layer(m_stream).async_connect(
        results, beast::bind_front_handler(&ClientSession::onConnect, shared_from_this()));
}

void ClientSession::onConnect(beast::error_code ec, const tcp::endpoint&)
{
    if (ec)
        return fail(ec);

    m_stage = "handshake";
    arm();
    m_stream.async_handshake(ssl::stream_base::client,
                             beast::bind_front_handler(&ClientSession::onHandshake, shared_from_this()));
}

void ClientSession::onHandshake(beast::error_code ec)
{
    if (ec)
        return fail(ec);

    m_out.tlsOk = true;

    X509* cert = SSL_get_peer_certificate(m_stream.native_handle());
    m_out.fingerprint = sha256Fingerprint(cert);
    m_out.certSubject = subjectLine(cert);
    if (cert)
        X509_free(cert);

    // ── Pin gate ────────────────────────────────────────────────────────────────────────────
    // Nothing is written until the peer is known to be the pinned device, so a man-in-the-middle
    // never receives the credential.
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
    beast::http::async_write(m_stream, m_httpReq,
                             beast::bind_front_handler(&ClientSession::onWrite, shared_from_this()));
}

void ClientSession::onWrite(beast::error_code ec, std::size_t)
{
    if (ec)
        return fail(ec);

    m_stage = "read";
    arm();
    beast::http::async_read(m_stream, m_buffer, m_httpRes,
                            beast::bind_front_handler(&ClientSession::onRead, shared_from_this()));
}

void ClientSession::onRead(beast::error_code ec, std::size_t)
{
    if (ec)
        return fail(ec);

    m_out.status = static_cast<int>(m_httpRes.result_int());
    m_out.body = m_httpRes.body();
    finish();
}

}
