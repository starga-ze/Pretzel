#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace pz::mgmtd
{

class AuthService;
class HttpListener;
class MetricService;
class MgmtdServiceManager;

class HttpServer
{
public:
    HttpServer(std::string listenAddress,
               std::uint16_t listenPort,
               bool tlsEnabled,
               std::string certFile,
               std::string keyFile,
               MetricService* metricService,
               AuthService* authService,
               MgmtdServiceManager* serviceManager);

    bool init();
    bool poll();
    void stop();

private:
    bool initTlsContext();
    std::string resolvePath(const std::string& path) const;

private:
    std::string m_listenAddress;
    std::uint16_t m_listenPort {0};

    bool m_tlsEnabled {false};
    std::string m_certFile;
    std::string m_keyFile;

    MetricService*       m_metricService    {nullptr};
    AuthService*         m_authService      {nullptr};
    MgmtdServiceManager* m_serviceManager   {nullptr};

    boost::asio::io_context m_ioContext;
    std::shared_ptr<boost::asio::ssl::context> m_sslContext;
    std::shared_ptr<HttpListener> m_listener;
};

} // namespace pz::mgmtd
