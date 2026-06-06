#include "EnginedCore.h"
#include "util/Logger.h"

namespace nf::engined
{

EnginedCore::EnginedCore()
    : Core("engined")
{
}

bool EnginedCore::onInit()
{
    const auto& cfg = m_config.json();

    const auto& log = cfg["logger"];
    m_loggerConfig.name = log["name"];
    m_loggerConfig.file = log["file"];
    m_loggerConfig.maxFileSize = log["max_file_size"];
    m_loggerConfig.maxFiles = log["max_files"];

    const auto& ipc = cfg["ipc"];

    m_ipcConfig.socketPath = ipc["socket_path"];
    m_ipcConfig.maxConnections = ipc["max_connections"];
    m_ipcConfig.maxFrameSize = ipc["max_frame_size"];
    m_ipcConfig.rxBufferSize = ipc["rx_buffer_size"];
    m_ipcConfig.txBufferSize = ipc["tx_buffer_size"];

    nf::util::Logger::Init(
            m_loggerConfig.name,
            m_loggerConfig.file,
            m_loggerConfig.maxFileSize,
            m_loggerConfig.maxFiles);

    LOG_INFO("EnginedCore onInit()...");

    m_threadManager = std::make_unique<nf::util::ThreadManager>();
    if (!m_threadManager)
    {
        LOG_ERROR("ThreadManager init failed");
        return false;
    }

    m_ipcClient = std::make_unique<nf::ipc::IpcClient>(m_ipcConfig, nf::ipc::IpcDaemon::Engined);

    if (!m_ipcClient->init())
    {
        LOG_ERROR("IpcClient init failed");
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
        LOG_ERROR("EnginedServiceManager init failed");
        return false;
    }

    m_rxRouter = std::make_unique<EnginedRxRouter>(m_eventFactory.get(), m_serviceManager.get());

    if (!m_rxRouter)
    {
        LOG_ERROR("EnginedRxRouter init failed");
        return false;
    }

    m_process = std::make_unique<EnginedProcess>(m_ipcClient.get(), m_serviceManager.get());

    if (!m_process)
    {
        LOG_ERROR("EnginedProcess init failed");
        return false;
    }

    m_ipcClient->handler()->setRxRouter(m_rxRouter.get());

    return true;
}

void EnginedCore::onLoop()
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

void EnginedCore::onShutdown()
{
    LOG_INFO("EnginedCore onShutdown()...");

    if (m_threadManager)
    {
        m_threadManager->stopAll();
    }

    LOG_INFO("All threads terminated successfully");

    nf::util::Logger::Shutdown();
}

} // namespace nf::engined
