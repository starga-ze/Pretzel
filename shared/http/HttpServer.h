#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace pz::http
{

class HttpHandler;
class HttpListener;

class HttpServer
{
public:
    HttpServer(std::string listenAddress, std::uint16_t listenPort, bool tlsEnabled, std::string certFile,
               std::string keyFile, std::string serverName, std::shared_ptr<HttpHandler> handler);

    bool init();
    bool poll();
    void stop();

private:
    bool initTlsContext();
    std::string resolvePath(const std::string& path) const;

private:
    std::string m_listenAddress;
    std::uint16_t m_listenPort{0};

    bool m_tlsEnabled{false};
    std::string m_certFile;
    std::string m_keyFile;
    std::string m_serverName;

    std::shared_ptr<HttpHandler> m_handler;

    boost::asio::io_context m_ioContext;
    std::shared_ptr<boost::asio::ssl::context> m_sslContext;
    std::shared_ptr<HttpListener> m_listener;
};

}
