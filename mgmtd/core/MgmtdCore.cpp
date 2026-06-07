#include "core/MgmtdCore.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <chrono>
#include <thread>

namespace pz::mgmtd
{

MgmtdCore::MgmtdCore()
    : Core("mgmtd")
{
}

bool MgmtdCore::onInit()
{
    if (!loadLoggerConfig())
    {
        return false;
    }

    pz::util::Logger::Init(m_loggerConfig.name,
                           m_loggerConfig.file,
                           m_loggerConfig.maxFileSize,
                           m_loggerConfig.maxFiles);

    LOG_INFO("MgmtdCore onInit()...");

    if (!loadIpcConfig() || !loadHttpConfig())
    {
        return false;
    }

    m_ipcClient = std::make_unique<pz::ipc::IpcClient>(m_ipcConfig, pz::ipc::IpcDaemon::Mgmtd);
    if (!m_ipcClient->init())
    {
        LOG_WARN("Mgmtd IpcClient init failed. Continue with HTTP metrics only.");
        m_ipcClient.reset();
    }

    m_eventFactory  = std::make_unique<MgmtdEventFactory>();
    m_actionFactory = std::make_unique<MgmtdActionFactory>();

    m_txRouter = std::make_unique<MgmtdTxRouter>(
        m_ipcClient ? m_ipcClient->handler() : nullptr);

    m_serviceManager = std::make_unique<MgmtdServiceManager>(
        m_eventFactory.get(),
        m_actionFactory.get(),
        m_txRouter.get());

    if (!m_serviceManager)
    {
        LOG_ERROR("Mgmtd service manager init failed");
        return false;
    }

    if (!loadAuthConfig())
    {
        return false;
    }

    m_rxRouter = std::make_unique<MgmtdRxRouter>(m_eventFactory.get(), m_serviceManager.get());

    if (m_ipcClient)
    {
        m_ipcClient->handler()->setRxRouter(m_rxRouter.get());
    }

    m_httpServer = std::make_unique<HttpServer>(m_httpConfig.listenAddress,
                                                m_httpConfig.listenPort,
                                                m_httpConfig.tlsEnabled,
                                                m_httpConfig.certFile,
                                                m_httpConfig.keyFile,
                                                &m_serviceManager->metricService(),
                                                &m_serviceManager->authService(),
                                                m_serviceManager.get());
    if (!m_httpServer || !m_httpServer->init())
    {
        LOG_ERROR("Mgmtd HttpServer init failed");
        return false;
    }

    m_process = std::make_unique<MgmtdProcess>(m_ipcClient.get(),
                                               m_httpServer.get(),
                                               m_serviceManager.get());
    if (!m_process)
    {
        LOG_ERROR("Mgmtd process init failed");
        return false;
    }

    return true;
}

void MgmtdCore::onLoop()
{
    if (!m_process->start())
    {
        LOG_ERROR("Mgmtd process start failed");
        return;
    }

    while (!stopping())
    {
        m_process->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void MgmtdCore::onShutdown()
{
    LOG_INFO("MgmtdCore onShutdown()...");

    if (m_httpServer)
    {
        m_httpServer->stop();
    }

    pz::util::Logger::Shutdown();
}

bool MgmtdCore::loadLoggerConfig()
{
    const auto& cfg = m_config.json();

    if (!cfg.contains("logger"))
    {
        m_loggerConfig.name = "pz-mgmtd";
        m_loggerConfig.file = "/tmp/pz-mgmtd.log";
        m_loggerConfig.maxFileSize = 5 * 1024 * 1024;
        m_loggerConfig.maxFiles = 10;
        return true;
    }

    const auto& log = cfg["logger"];
    m_loggerConfig.name = log.value("name", "pz-mgmtd");
    m_loggerConfig.file = log.value("file", "/tmp/pz-mgmtd.log");
    m_loggerConfig.maxFileSize = log.value("max_file_size", 5 * 1024 * 1024);
    m_loggerConfig.maxFiles = log.value("max_files", 10);
    return true;
}

bool MgmtdCore::loadIpcConfig()
{
    const auto& cfg = m_config.json();

    if (!cfg.contains("ipc"))
    {
        m_ipcConfig.socketPath = "/run/pretzel/ipcd.sock";
        return true;
    }

    const auto& ipc = cfg["ipc"];
    m_ipcConfig.socketPath = ipc.value("socket_path", "/run/pretzel/ipcd.sock");
    m_ipcConfig.maxConnections = ipc.value("max_connections", 128);
    m_ipcConfig.maxFrameSize = ipc.value("max_frame_size", 65536);
    m_ipcConfig.rxBufferSize = ipc.value("rx_buffer_size", 65536);
    m_ipcConfig.txBufferSize = ipc.value("tx_buffer_size", 65536);
    return true;
}

bool MgmtdCore::loadHttpConfig()
{
    const auto& cfg = m_config.json();

    if (!cfg.contains("http"))
    {
        return true;
    }

    const auto& http = cfg["http"];
    m_httpConfig.listenAddress = http.value("listen_address", "0.0.0.0");
    m_httpConfig.listenPort =
        static_cast<std::uint16_t>(http.value("listen_port", 9101));

    m_httpConfig.tlsEnabled = http.value("tls_enabled", false);
    m_httpConfig.certFile = http.value("cert_file", "");
    m_httpConfig.keyFile = http.value("key_file", "");

    return true;
}

bool MgmtdCore::loadAuthConfig()
{
    if (!m_serviceManager)
    {
        return true;
    }

    const auto& cfg = m_config.json();
    if (!cfg.contains("admin"))
    {
        return true;
    }

    const auto& admin = cfg["admin"];
    m_serviceManager->authService().load(admin.value("username", "admin"),
                                         admin.value("password_hash", ""),
                                         admin.value("salt", ""));
    return true;
}

} // namespace pz::mgmtd
