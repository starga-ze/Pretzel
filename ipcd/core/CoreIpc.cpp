#include "CoreIpc.h"
#include "util/Logger.h"


#include <thread>

namespace nf::ipcd
{

constexpr int kIpcServerTimeoutMs = 10;

CoreIpc::CoreIpc() : Core("ipcd")
{
}

bool CoreIpc::onInit()
{
    initConfig();
    initLogger();

    LOG_INFO("CoreIpc init...");

    initIpcRuntime();

    return true;
}

void CoreIpc::onLoop()
{
    LOG_INFO("CoreIpc Runtime Loop Started");

    while (!stopping())
    {
        m_ipcServer->poll(kIpcServerTimeoutMs);

        processRuntime();
    }

    LOG_INFO("CoreIpc Runtime Loop Stopped");
}

void CoreIpc::onShutdown()
{
    LOG_INFO("CoreIpc shutdown...");

    nf::util::Logger::Shutdown();
}

void CoreIpc::initConfig()
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
}

void CoreIpc::initLogger()
{
    nf::util::Logger::Init(
            m_loggerConfig.name,
            m_loggerConfig.file,
            m_loggerConfig.maxFileSize,
            m_loggerConfig.maxFiles
            );
}

bool CoreIpc::initIpcRuntime()
{
    m_ipcServer = std::make_unique<IpcServer>(m_ipcConfig, nf::ipc::IpcDaemon::Ipcd);
    
    if (!m_ipcServer->init())
    {
        LOG_ERROR("IpcServer init failed");
        return false;
    }

    m_txRouter = std::make_unique<IpcdTxRouter>(m_ipcServer->handler());
    m_rxRouter = std::make_unique<IpcdRxRouter>(m_txRouter.get());

    m_ipcServer->handler()->setRxRouter(m_rxRouter.get());

    return true;
}

void CoreIpc::processRuntime()
{
}

} // namespace nf::ipcd
