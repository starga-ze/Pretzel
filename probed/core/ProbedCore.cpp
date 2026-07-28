#include "ProbedCore.h"
#include "util/Logger.h"

namespace pz::probed
{

ProbedCore::ProbedCore() : Core("probed")
{
}

bool ProbedCore::onInit()
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

    pz::util::Logger::Init(m_loggerConfig.name, m_loggerConfig.file, m_loggerConfig.maxFileSize,
                           m_loggerConfig.maxFiles);

    LOG_INFO("probed: starting up");

    m_threadManager = std::make_unique<pz::util::ThreadManager>();
    if (!m_threadManager)
    {
        LOG_ERROR("failed to initialize thread manager");
        return false;
    }

    m_ipcClient = std::make_unique<pz::ipc::IpcClient>(m_ipcConfig, pz::ipc::IpcDaemon::Probed);

    if (!m_ipcClient->init())
    {
        LOG_ERROR("failed to initialize IPC client");
        return false;
    }

    m_icmpEngine = std::make_unique<IcmpEngine>();

    if (!m_icmpEngine->init())
    {
        LOG_ERROR("IcmpEngine init failed");
        return false;
    }

    m_eventFactory = std::make_unique<ProbedEventFactory>();
    m_actionFactory = std::make_unique<ProbedActionFactory>();

    m_rxRouter = std::make_unique<ProbedRxRouter>(m_eventFactory.get());
    m_txRouter = std::make_unique<ProbedTxRouter>(m_ipcClient->handler(), m_icmpEngine->handler());

    if (!m_txRouter or !m_rxRouter)
    {
        LOG_ERROR("failed to initialize IPC router");
        return false;
    }

    m_ioContext = std::make_unique<boost::asio::io_context>();

    m_serviceManager = std::make_unique<ProbedServiceManager>(m_eventFactory.get(), m_actionFactory.get(),
                                                              m_txRouter.get(), m_ioContext.get());
    if (!m_serviceManager)
    {
        LOG_ERROR("failed to initialize service manager");
        return false;
    }

    m_process = std::make_unique<ProbedProcess>(m_ipcClient.get(), m_icmpEngine.get(), m_serviceManager.get(),
                                                m_ioContext.get());

    if (!m_process)
    {
        LOG_ERROR("failed to initialize process");
        return false;
    }

    m_ipcClient->handler()->setRxRouter(m_rxRouter.get());
    m_icmpEngine->handler()->setRxRouter(m_rxRouter.get());

    m_rxRouter->setServiceManager(m_serviceManager.get());

    return true;
}

void ProbedCore::onLoop()
{
    if (!m_process->start())
    {
        LOG_ERROR("process failed to start");
        return;
    }

    while (!stopping())
    {
        checkReload();
        m_process->tick();
    }
}

void ProbedCore::onShutdown()
{
    LOG_INFO("shutting down");

    if (m_threadManager)
    {
        m_threadManager->stopAll();
    }

    LOG_INFO("all threads stopped");

    pz::util::Logger::Shutdown();
}

}
