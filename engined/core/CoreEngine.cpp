#include "CoreEngine.h"
#include "util/Logger.h"

#include <thread>

namespace nf::engined
{

CoreEngine::CoreEngine() : Core("engined")
{
}

bool CoreEngine::onInit()
{
    /* Config init */
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

    /* Logger init */
    nf::util::Logger::Init(
            m_loggerConfig.name,
            m_loggerConfig.file,
            m_loggerConfig.maxFileSize,
            m_loggerConfig.maxFiles);

    LOG_INFO("Engined onInit()...");

    /* ThreadManager init */
    m_threadManager = std::make_unique<ThreadManager>();
    if (!m_threadManager)
    {
        LOG_FATAL("ThreadManager init failed");
        return false;
    }

    /* Ipc init */
    m_ipcClient = std::make_unique<IpcClient>(m_ipcConfig, nf::ipc::IpcDaemon::Engined);

    if (!m_ipcClient->init())
    {
        LOG_FATAL("IpcClient init failed");
        return false;
    }

    /* Router init */
    m_txRouter = std::make_unique<EnginedTxRouter>(m_ipcClient->handler());
    m_rxRouter = std::make_unique<EnginedRxRouter>(m_txRouter.get());

    if (!m_txRouter or !m_rxRouter)
    {
        LOG_FATAL("IpcRouter init failed");
        return false;
    }

    m_ipcClient->handler()->setRxRouter(m_rxRouter.get());

    /* Process init */
    m_process = std::make_unique<EnginedProcess>(m_ipcClient.get(), m_txRouter.get());

    if (!m_process)
    {
        LOG_FATAL("Process init failed");
        return false;
    }

    return true;
}

void CoreEngine::onLoop()
{
    while (!stopping())
    {
        m_process->tick(); 
    }
}

void CoreEngine::onShutdown()
{
    LOG_INFO("CoreEngine onShutdown()...");

    m_threadManager->stopAll();

    LOG_INFO("All threads terminated successfully");

    nf::util::Logger::Shutdown();
}

} // namespace nf::ipcd
