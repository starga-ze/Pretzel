#include "core/ApidCore.h"

#include "http/ApidHttpHandler.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

namespace pz::apid
{

ApidCore::ApidCore()
    : Core("apid")
{
}

bool ApidCore::onInit()
{
    if (!loadLoggerConfig())
    {
        return false;
    }

    pz::util::Logger::Init(m_loggerConfig.name,
                           m_loggerConfig.file,
                           m_loggerConfig.maxFileSize,
                           m_loggerConfig.maxFiles);

    LOG_INFO("apid: starting up");

    if (!loadIpcConfig() || !loadHttpConfig())
    {
        return false;
    }

    m_ipcClient = std::make_unique<pz::ipc::IpcClient>(m_ipcConfig, pz::ipc::IpcDaemon::Apid);
    if (!m_ipcClient->init())
    {
        LOG_WARN("IPC client unavailable — ingest edge runs, reports are not forwarded");
        m_ipcClient.reset();
    }

    m_eventFactory  = std::make_unique<ApidEventFactory>();
    m_actionFactory = std::make_unique<ApidActionFactory>();

    m_txRouter = std::make_unique<ApidTxRouter>(
        m_ipcClient ? m_ipcClient->handler() : nullptr);

    m_serviceManager = std::make_unique<ApidServiceManager>(
        m_eventFactory.get(),
        m_actionFactory.get(),
        m_txRouter.get());

    m_rxRouter = std::make_unique<ApidRxRouter>(m_eventFactory.get(), m_serviceManager.get());

    if (m_ipcClient)
    {
        m_ipcClient->handler()->setRxRouter(m_rxRouter.get());
    }

    // Handler layer: a thin transport adapter (beast <-> DTO) that forwards requests to
    // the unified RxRouter — the same router that handles IPC ingress.
    auto httpHandler = std::make_shared<ApidHttpHandler>(m_rxRouter.get());

    m_httpServer = std::make_unique<pz::http::HttpServer>(m_httpConfig.listenAddress,
                                                          m_httpConfig.listenPort,
                                                          m_httpConfig.tlsEnabled,
                                                          m_httpConfig.certFile,
                                                          m_httpConfig.keyFile,
                                                          "pz-apid",
                                                          std::move(httpHandler));
    if (!m_httpServer || !m_httpServer->init())
    {
        LOG_ERROR("failed to initialize HTTP server");
        return false;
    }

    m_process = std::make_unique<ApidProcess>(m_ipcClient.get(),
                                              m_httpServer.get(),
                                              m_serviceManager.get());
    return true;
}

void ApidCore::onLoop()
{
    if (!m_process->start())
    {
        LOG_ERROR("failed to start process");
        return;
    }

    while (!stopping())
    {
        m_process->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ApidCore::onShutdown()
{
    LOG_INFO("shutting down");

    if (m_httpServer)
    {
        m_httpServer->stop();
    }

    pz::util::Logger::Shutdown();
}

bool ApidCore::loadLoggerConfig()
{
    const auto& cfg = m_config.json();
    const auto sys = cfg.value("system", nlohmann::json::object());

    const auto log = sys.value("logger", nlohmann::json::object());
    m_loggerConfig.name = log.value("name", "pz-apid");
    m_loggerConfig.file = log.value("file", "/tmp/pz-apid.log");
    m_loggerConfig.maxFileSize = log.value("max_file_size", 5 * 1024 * 1024);
    m_loggerConfig.maxFiles = log.value("max_files", 10);
    return true;
}

bool ApidCore::loadIpcConfig()
{
    const auto& cfg = m_config.json();
    const auto sys = cfg.value("system", nlohmann::json::object());

    const auto ipc = sys.value("ipc", nlohmann::json::object());
    m_ipcConfig.socketPath = ipc.value("socket_path", "/run/pretzel/ipcd.sock");
    m_ipcConfig.maxConnections = ipc.value("max_connections", 128);
    m_ipcConfig.maxFrameSize = ipc.value("max_frame_size", 1048576);
    m_ipcConfig.rxBufferSize = ipc.value("rx_buffer_size", 1048576);
    m_ipcConfig.txBufferSize = ipc.value("tx_buffer_size", 1048576);
    return true;
}

bool ApidCore::loadHttpConfig()
{
    const auto& cfg = m_config.json();
    const auto svc = cfg.value("service", nlohmann::json::object());

    const auto http = svc.value("http", nlohmann::json::object());
    m_httpConfig.listenAddress = http.value("listen_address", "0.0.0.0");
    m_httpConfig.listenPort =
        static_cast<std::uint16_t>(http.value("listen_port", 8443));
    m_httpConfig.tlsEnabled = http.value("tls_enabled", false);
    m_httpConfig.certFile = http.value("cert_file", "");
    m_httpConfig.keyFile = http.value("key_file", "");

    return true;
}

} // namespace pz::apid
