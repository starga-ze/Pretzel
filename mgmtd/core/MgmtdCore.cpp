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

    LOG_INFO("mgmtd: starting up");

    if (!loadIpcConfig() || !loadHttpConfig())
    {
        return false;
    }

    m_ipcClient = std::make_unique<pz::ipc::IpcClient>(m_ipcConfig, pz::ipc::IpcDaemon::Mgmtd);
    if (!m_ipcClient->init())
    {
        LOG_WARN("IPC client unavailable — running in metrics-only mode");
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
        LOG_ERROR("failed to initialize service manager");
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
        LOG_ERROR("failed to initialize HTTP server");
        return false;
    }

    m_process = std::make_unique<MgmtdProcess>(m_ipcClient.get(),
                                               m_httpServer.get(),
                                               m_serviceManager.get());
    if (!m_process)
    {
        LOG_ERROR("failed to initialize process");
        return false;
    }

    return true;
}

void MgmtdCore::onLoop()
{
    if (!m_process->start())
    {
        LOG_ERROR("failed to start process");
        return;
    }

    while (!stopping())
    {
        checkReload();
        m_process->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void MgmtdCore::onShutdown()
{
    LOG_INFO("shutting down");

    if (m_httpServer)
    {
        m_httpServer->stop();
    }

    pz::util::Logger::Shutdown();
}

bool MgmtdCore::loadLoggerConfig()
{
    const auto& cfg = m_config.json();
    const auto sys = cfg.value("system", nlohmann::json::object());

    if (!sys.contains("logger"))
    {
        m_loggerConfig.name = "pz-mgmtd";
        m_loggerConfig.file = "/tmp/pz-mgmtd.log";
        m_loggerConfig.maxFileSize = 5 * 1024 * 1024;
        m_loggerConfig.maxFiles = 10;
        return true;
    }

    const auto& log = sys["logger"];
    m_loggerConfig.name = log.value("name", "pz-mgmtd");
    m_loggerConfig.file = log.value("file", "/tmp/pz-mgmtd.log");
    m_loggerConfig.maxFileSize = log.value("max_file_size", 5 * 1024 * 1024);
    m_loggerConfig.maxFiles = log.value("max_files", 10);
    return true;
}

bool MgmtdCore::loadIpcConfig()
{
    const auto& cfg = m_config.json();
    const auto sys = cfg.value("system", nlohmann::json::object());

    if (!sys.contains("ipc"))
    {
        m_ipcConfig.socketPath = "/run/pretzel/ipcd.sock";
        return true;
    }

    const auto& ipc = sys["ipc"];
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
    const auto svc = cfg.value("service", nlohmann::json::object());

    if (!svc.contains("http"))
    {
        return true;
    }

    const auto& http = svc["http"];
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

    // The admin credential lives hashed in the running-config (mgmtd.service.http.admin),
    // seeded/written by engined (which boots first). mgmtd only reads it here; the
    // settings API redacts it from GUI responses.
    m_serviceManager->authService().loadCredential();
    return true;
}

} // namespace pz::mgmtd
