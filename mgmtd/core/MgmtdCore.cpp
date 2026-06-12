#include "core/MgmtdCore.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace pz::mgmtd
{

MgmtdCore::MgmtdCore()
    : Core("mgmtd")
{
}

void MgmtdCore::onPreConfigLoad()
{
    // mgmtd owns the config store: sync the startup-config file into the DB and seed
    // running_config v1 on a factory-fresh device (idempotent — existing history is
    // preserved). Runs before any daemon reads its config.
    m_storeSeeded = pz::config::Config::seedStore();
    if (!m_storeSeeded)
    {
        // Typically the boot-time DB race: PostgreSQL is up as a systemd unit but not
        // yet accepting connections, so seedStore() could not write. We continue on the
        // startup-config file fallback and ensureStoreSeeded() retries from onLoop()
        // once the DB is reachable — otherwise the config tables stay empty forever.
        std::cerr << "mgmtd: config store seed failed — continuing on startup-config "
                     "fallback; will retry once the DB is reachable" << std::endl;
    }
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
        ensureStoreSeeded();
        m_process->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void MgmtdCore::ensureStoreSeeded()
{
    if (m_storeSeeded)
    {
        return;
    }

    // Throttle retries so a persistently-down DB does not spin the loop or spam the
    // journal; the first attempt (epoch sentinel) runs immediately.
    const auto now = std::chrono::steady_clock::now();
    if (m_lastSeedAttempt != std::chrono::steady_clock::time_point{} &&
        now - m_lastSeedAttempt < std::chrono::seconds(3))
    {
        return;
    }
    m_lastSeedAttempt = now;

    if (!pz::config::Config::seedStore())
    {
        LOG_WARN("config store seed retry failed — DB still unavailable");
        return;
    }

    m_storeSeeded = true;

    // Adopt the freshly seeded running-config on the next read, and seed the default
    // admin account that loadFromDb() in onInit() could not write while the DB was down.
    pz::config::Config::invalidateConfigCache();
    if (m_serviceManager)
    {
        m_serviceManager->authService().loadFromDb();
    }

    LOG_INFO("config store seeded after DB became reachable");
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

    // The admin credential lives entirely in the admin_user DB table (hashed). It is
    // NOT read from config: redactSecretsForPersist() strips the admin block before it
    // is ever persisted, so the running-config mgmtd reads here never carries it.
    // loadFromDb() loads the stored hash, or seeds a default account on a factory-fresh
    // device. seedStore() in onPreConfigLoad has already ensured the schema + DB
    // connection by the time this runs.
    m_serviceManager->authService().loadFromDb();
    return true;
}

} // namespace pz::mgmtd
