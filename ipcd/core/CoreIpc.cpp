#include "CoreIpc.h"
#include "util/Logger.h"

#include <thread>

namespace nf::ipcd
{

CoreIpc::CoreIpc() : Core("ipcd")
{
}

bool CoreIpc::onInit()
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

    m_threadManager = std::make_unique<ThreadManager>();
    if (!m_threadManager)
    {
        LOG_ERROR("CoreIpc dependency failed");
        return false;
    }

    m_ipcServer = std::make_unique<IpcServer>(m_ipcConfig, nf::ipc::IpcDaemon::Ipcd);
    if (!m_ipcServer->init())
    {
        LOG_ERROR("IpcServer init failed");
        return false;
    }

    m_txRouter = std::make_unique<IpcdTxRouter>(m_ipcServer->handler());
    m_rxRouter = std::make_unique<IpcdRxRouter>(m_txRouter.get());
    if (!m_txRouter or !m_rxRouter)
    {
        LOG_ERROR("IpcRouter init failed");
        return false;
    }

    m_process = std::make_unique<IpcdProcess>(m_ipcServer.get(), m_txRouter.get());
    if (!m_process)
    {
        LOG_ERROR("Process init failed");
        return false;
    }

    m_ipcServer->handler()->setRxRouter(m_rxRouter.get());
    m_rxRouter->setProcess(m_process.get());

    return true;
}

void CoreIpc::onLoop()
{
    if (!m_process->start())
    {
        LOG_ERROR("Process Start Failed...");
        return;
    }

    while(!stopping())
    {
        m_process->tick();
    }
}

void CoreIpc::onShutdown()
{
    LOG_INFO("CoreIpc shutdown...");

    m_threadManager->stopAll();

    LOG_INFO("All threads terminated successfully");

    nf::util::Logger::Shutdown();
}



} // namespace nf::ipcd
