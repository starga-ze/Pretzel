#include "IcmpdCore.h"
#include "util/Logger.h"

namespace nf::icmpd
{

IcmpdCore::IcmpdCore() : 
    Core("icmpd")
{
}

bool IcmpdCore::onInit()
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

    LOG_INFO("Icmpd onInit()...");

    /* ThreadManager init */
    m_threadManager = std::make_unique<nf::util::ThreadManager>();
    if (!m_threadManager)
    {
        LOG_ERROR("ThreadManager init failed");
        return false;
    }

    /* Ipc init */
    m_ipcClient = std::make_unique<nf::ipc::IpcClient>(m_ipcConfig, nf::ipc::IpcDaemon::Icmpd);

    if (!m_ipcClient->init())
    {
        LOG_ERROR("IpcClient init failed");
        return false;
    }

    /* Router init */
    m_rxRouter = std::make_unique<IcmpdRxRouter>();
    m_txRouter = std::make_unique<IcmpdTxRouter>(m_ipcClient->handler());

    if (!m_txRouter or !m_rxRouter)
    {
        LOG_ERROR("IpcRouter init failed");
        return false;
    }

    /* Service init */
    m_serviceManager = std::make_unique<IcmpdServiceManager>();
    if (!m_serviceManager)
    {
        LOG_ERROR("Service init failed");
        return false;
    }

    /* Process init */
    m_process = std::make_unique<IcmpdProcess>(m_ipcClient.get(), m_serviceManager.get());

    if (!m_process)
    {
        LOG_ERROR("Process init failed");
        return false;
    }

    /* Binding */
    m_ipcClient->handler()->setRxRouter(m_rxRouter.get());
 
    m_rxRouter->setServiceManager(m_serviceManager.get());

    return true;
}

void IcmpdCore::onLoop()
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

void IcmpdCore::onShutdown()
{
    LOG_INFO("IcmpdCore onShutdown()...");

    if (m_threadManager)
    {
        m_threadManager->stopAll();
    }

    LOG_INFO("All threads terminated successfully");

    nf::util::Logger::Shutdown();
}

} // namespace nf::icmpd
