#include "TopologydCore.h"
#include "util/Logger.h"

namespace pz::topologyd
{

TopologydCore::TopologydCore()
    : Core("topologyd")
{
}

bool TopologydCore::onInit()
{
    const auto& cfg = m_config.json();

    const auto& log = cfg["logger"];
    m_loggerConfig.name        = log["name"];
    m_loggerConfig.file        = log["file"];
    m_loggerConfig.maxFileSize = log["max_file_size"];
    m_loggerConfig.maxFiles    = log["max_files"];

    const auto& ipc = cfg["ipc"];
    m_ipcConfig.socketPath    = ipc["socket_path"];
    m_ipcConfig.maxConnections = ipc["max_connections"];
    m_ipcConfig.maxFrameSize  = ipc["max_frame_size"];
    m_ipcConfig.rxBufferSize  = ipc["rx_buffer_size"];
    m_ipcConfig.txBufferSize  = ipc["tx_buffer_size"];

    pz::util::Logger::Init(
        m_loggerConfig.name,
        m_loggerConfig.file,
        m_loggerConfig.maxFileSize,
        m_loggerConfig.maxFiles);

    LOG_INFO("TopologydCore onInit()...");

    m_threadManager = std::make_unique<pz::util::ThreadManager>();
    if (!m_threadManager)
    {
        LOG_ERROR("ThreadManager init failed");
        return false;
    }

    m_ipcClient = std::make_unique<pz::ipc::IpcClient>(m_ipcConfig,
                                                        pz::ipc::IpcDaemon::Topologyd);

    if (!m_ipcClient->init())
    {
        LOG_ERROR("IpcClient init failed");
        return false;
    }

    m_eventFactory  = std::make_unique<TopologydEventFactory>();
    m_actionFactory = std::make_unique<TopologydActionFactory>();

    m_rxRouter = std::make_unique<TopologydRxRouter>(m_eventFactory.get());
    m_txRouter = std::make_unique<TopologydTxRouter>(m_ipcClient->handler());

    if (!m_txRouter or !m_rxRouter)
    {
        LOG_ERROR("IpcRouter init failed");
        return false;
    }

    m_serviceManager = std::make_unique<TopologydServiceManager>(
        m_eventFactory.get(),
        m_actionFactory.get(),
        m_txRouter.get());

    if (!m_serviceManager)
    {
        LOG_ERROR("TopologydServiceManager init failed");
        return false;
    }

    m_process = std::make_unique<TopologydProcess>(m_ipcClient.get(), m_serviceManager.get());

    if (!m_process)
    {
        LOG_ERROR("TopologydProcess init failed");
        return false;
    }

    m_ipcClient->handler()->setRxRouter(m_rxRouter.get());
    m_rxRouter->setServiceManager(m_serviceManager.get());

    return true;
}

void TopologydCore::onLoop()
{
    if (!m_process->start())
    {
        LOG_ERROR("Process Start Failed...");
        return;
    }

    while (!stopping())
    {
        m_process->tick();
    }
}

void TopologydCore::onShutdown()
{
    LOG_INFO("TopologydCore onShutdown()...");

    if (m_threadManager)
    {
        m_threadManager->stopAll();
    }

    LOG_INFO("All threads terminated successfully");

    pz::util::Logger::Shutdown();
}

} // namespace pz::topologyd
