#include "EnginedCore.h"
#include "util/Logger.h"

#include <iostream>

namespace pz::engined
{

EnginedCore::EnginedCore()
    : Core("engined")
{
}

void EnginedCore::onPreConfigLoad()
{
    // engined owns the config store: connect, create the schema (single-writer DDL),
    // and seed running_config v1 on a factory-fresh device. Runs before any daemon —
    // including engined itself — reads its config. Logger is not up yet, so use stderr.
    m_preflighted = pz::config::Config::preflight();
    if (!m_preflighted)
    {
        // Typically the boot-time DB race: PostgreSQL is up as a systemd unit but not
        // yet accepting connections. We continue on the startup-config fallback and
        // ensureStorePreflighted() retries from onLoop() once the DB is reachable.
        std::cerr << "engined: config store preflight failed — continuing on "
                     "startup-config fallback; will retry once the DB is reachable"
                  << std::endl;
    }
}

bool EnginedCore::onInit()
{
    const auto& cfg = m_config.json();

    const auto& log = cfg["system"]["logger"];
    m_loggerConfig.name = log["name"];
    m_loggerConfig.file = log["file"];
    m_loggerConfig.maxFileSize = log["max_file_size"];
    m_loggerConfig.maxFiles = log["max_files"];

    const auto& ipc = cfg["system"]["ipc"];

    m_ipcConfig.socketPath = ipc["socket_path"];
    m_ipcConfig.maxConnections = ipc["max_connections"];
    m_ipcConfig.maxFrameSize = ipc["max_frame_size"];
    m_ipcConfig.rxBufferSize = ipc["rx_buffer_size"];
    m_ipcConfig.txBufferSize = ipc["tx_buffer_size"];

    pz::util::Logger::Init(
            m_loggerConfig.name,
            m_loggerConfig.file,
            m_loggerConfig.maxFileSize,
            m_loggerConfig.maxFiles);

    LOG_INFO("engined: starting up");

    m_threadManager = std::make_unique<pz::util::ThreadManager>();
    if (!m_threadManager)
    {
        LOG_ERROR("failed to initialize thread manager");
        return false;
    }

    m_ipcClient = std::make_unique<pz::ipc::IpcClient>(m_ipcConfig, pz::ipc::IpcDaemon::Engined);

    if (!m_ipcClient->init())
    {
        LOG_ERROR("failed to initialize IPC client");
        return false;
    }

    m_eventFactory  = std::make_unique<EnginedEventFactory>();
    m_actionFactory = std::make_unique<EnginedActionFactory>();

    m_txRouter = std::make_unique<EnginedTxRouter>(m_ipcClient->handler());

    m_serviceManager = std::make_unique<EnginedServiceManager>(
        m_eventFactory.get(),
        m_actionFactory.get(),
        m_txRouter.get());

    if (!m_serviceManager)
    {
        LOG_ERROR("failed to initialize service manager");
        return false;
    }

    m_rxRouter = std::make_unique<EnginedRxRouter>(m_eventFactory.get(), m_serviceManager.get());

    if (!m_rxRouter)
    {
        LOG_ERROR("failed to initialize RX router");
        return false;
    }

    m_process = std::make_unique<EnginedProcess>(m_ipcClient.get(), m_serviceManager.get());

    if (!m_process)
    {
        LOG_ERROR("failed to initialize process");
        return false;
    }

    m_ipcClient->handler()->setRxRouter(m_rxRouter.get());

    return true;
}

void EnginedCore::onLoop()
{
    if (!m_process->start())
    {
        LOG_ERROR("process failed to start");
        return;
    }

    while (!stopping())
    {
        checkReload();
        ensureStorePreflighted();
        m_process->tick();
    }
}

void EnginedCore::ensureStorePreflighted()
{
    if (m_preflighted)
    {
        return;
    }

    // Throttle retries so a persistently-down DB does not spin the loop or spam the
    // journal; the first attempt (epoch sentinel) runs immediately.
    const auto now = std::chrono::steady_clock::now();
    if (m_lastPreflightAttempt != std::chrono::steady_clock::time_point{} &&
        now - m_lastPreflightAttempt < std::chrono::seconds(3))
    {
        return;
    }
    m_lastPreflightAttempt = now;

    if (!pz::config::Config::preflight())
    {
        LOG_WARN("config store preflight retry failed — DB still unavailable");
        return;
    }

    m_preflighted = true;

    // Adopt the freshly seeded running-config on the next read.
    pz::config::Config::invalidateConfigCache();

    LOG_INFO("config store preflighted after DB became reachable");
}

void EnginedCore::onShutdown()
{
    LOG_INFO("shutting down");

    if (m_threadManager)
    {
        m_threadManager->stopAll();
    }

    LOG_INFO("all threads stopped");

    pz::util::Logger::Shutdown();
}

} // namespace pz::engined
