#include "http/HttpServer.h"

#include "http/HttpHandler.h"
#include "http/HttpListener.h"
#include "util/Logger.h"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>

#include <cstdlib>

namespace pz::http
{

namespace
{

std::string configDir()
{
    const char* value = std::getenv("PRETZEL_CONFIG_DIR");
    return (value && *value) ? std::string(value) : "/etc/pretzel";
}

}

HttpServer::HttpServer(std::string listenAddress, std::uint16_t listenPort, bool tlsEnabled, std::string certFile,
                       std::string keyFile, std::string serverName, std::shared_ptr<HttpHandler> handler)
    : m_listenAddress(std::move(listenAddress)), m_listenPort(listenPort), m_tlsEnabled(tlsEnabled),
      m_certFile(std::move(certFile)), m_keyFile(std::move(keyFile)), m_serverName(std::move(serverName)),
      m_handler(std::move(handler))
{
}

std::string HttpServer::resolvePath(const std::string& path) const
{
    if (path.empty())
    {
        return path;
    }

    if (path.front() == '/')
    {
        return path;
    }

    return configDir() + "/" + path;
}

bool HttpServer::initTlsContext()
{
    if (!m_tlsEnabled)
    {
        return true;
    }

    if (m_certFile.empty() || m_keyFile.empty())
    {
        LOG_ERROR("certificate or key path is not configured");
        return false;
    }

    m_sslContext = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls_server);

    boost::system::error_code ec;

    m_sslContext->set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 |
                                  boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::single_dh_use,
                              ec);

    if (ec)
    {
        LOG_ERROR("HTTPS set_options failed (error={})", ec.message());
        return false;
    }

    const auto certPath = resolvePath(m_certFile);
    const auto keyPath = resolvePath(m_keyFile);

    m_sslContext->use_certificate_chain_file(certPath, ec);
    if (ec)
    {
        LOG_ERROR("HTTPS load cert failed (path={}, error={})", certPath, ec.message());
        return false;
    }

    m_sslContext->use_private_key_file(keyPath, boost::asio::ssl::context::pem, ec);

    if (ec)
    {
        LOG_ERROR("HTTPS load key failed (path={}, error={})", keyPath, ec.message());
        return false;
    }

    LOG_INFO("HTTPS enabled (cert={}, key={})", certPath, keyPath);
    return true;
}

bool HttpServer::init()
{
    boost::system::error_code ec;
    const auto address = boost::asio::ip::make_address(m_listenAddress, ec);
    if (ec)
    {
        LOG_ERROR("invalid listen address (address={}, error={})", m_listenAddress, ec.message());
        return false;
    }

    if (!initTlsContext())
    {
        return false;
    }

    const auto endpoint = boost::asio::ip::tcp::endpoint(address, m_listenPort);
    m_listener = std::make_shared<HttpListener>(m_ioContext, endpoint, m_handler, m_sslContext, m_serverName);

    if (!m_listener->open())
    {
        return false;
    }

    m_listener->run();
    return true;
}

bool HttpServer::poll()
{
    m_ioContext.poll();
    return true;
}

void HttpServer::stop()
{
    m_ioContext.stop();
}

}
