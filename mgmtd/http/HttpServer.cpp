#include "http/HttpServer.h"

#include "http/HttpCache.h"
#include "http/HttpListener.h"
#include "http/HttpRouter.h"
#include "service/MgmtdServiceManager.h"
#include "util/Logger.h"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl.hpp>

namespace nf::mgmtd
{

HttpServer::HttpServer(std::string listenAddress,
                       std::uint16_t listenPort,
                       bool tlsEnabled,
                       std::string certFile,
                       std::string keyFile,
                       MetricService* metricService,
                       AuthService* authService,
                       MgmtdServiceManager* serviceManager)
    : m_listenAddress(std::move(listenAddress)),
      m_listenPort(listenPort),
      m_tlsEnabled(tlsEnabled),
      m_certFile(std::move(certFile)),
      m_keyFile(std::move(keyFile)),
      m_metricService(metricService),
      m_authService(authService),
      m_serviceManager(serviceManager)
{
}

std::string HttpServer::resolvePath(const std::string& path) const
{
    if (path.empty())
    {
        return path;
    }

    if (!path.empty() && path.front() == '/')
    {
        return path;
    }

    return std::string(PROJECT_ROOT) + "/" + path;
}

bool HttpServer::initTlsContext()
{
    if (!m_tlsEnabled)
    {
        return true;
    }

    if (m_certFile.empty() || m_keyFile.empty())
    {
        LOG_ERROR("Mgmtd HTTPS enabled but cert_file or key_file is empty");
        return false;
    }

    m_sslContext = std::make_shared<boost::asio::ssl::context>(
        boost::asio::ssl::context::tls_server);

    boost::system::error_code ec;

    m_sslContext->set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::single_dh_use,
        ec);

    if (ec)
    {
        LOG_ERROR("Mgmtd HTTPS set_options failed: {}", ec.message());
        return false;
    }

    const auto certPath = resolvePath(m_certFile);
    const auto keyPath = resolvePath(m_keyFile);

    m_sslContext->use_certificate_chain_file(certPath, ec);
    if (ec)
    {
        LOG_ERROR("Mgmtd HTTPS load cert failed path={} error={}", certPath, ec.message());
        return false;
    }

    m_sslContext->use_private_key_file(
        keyPath,
        boost::asio::ssl::context::pem,
        ec);

    if (ec)
    {
        LOG_ERROR("Mgmtd HTTPS load key failed path={} error={}", keyPath, ec.message());
        return false;
    }

    LOG_INFO("Mgmtd HTTPS enabled cert={} key={}", certPath, keyPath);
    return true;
}

bool HttpServer::init()
{
    boost::system::error_code ec;
    const auto address = boost::asio::ip::make_address(m_listenAddress, ec);
    if (ec)
    {
        LOG_ERROR("Mgmtd HTTP invalid listen address {}: {}", m_listenAddress, ec.message());
        return false;
    }

    if (!initTlsContext())
    {
        return false;
    }

    auto cache = std::make_shared<HttpCache>(std::string(PROJECT_ROOT) + "/mgmtd/www");
    auto router = std::make_shared<HttpRouter>(m_metricService, m_authService, m_serviceManager, cache);

    const auto endpoint = boost::asio::ip::tcp::endpoint(address, m_listenPort);
    m_listener = std::make_shared<HttpListener>(m_ioContext,
                                                endpoint,
                                                router,
                                                m_sslContext);

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

} // namespace nf::mgmtd
