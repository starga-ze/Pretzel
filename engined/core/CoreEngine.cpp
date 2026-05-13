#include "CoreEngine.h"
#include "util/Logger.h"

#include <thread>
#include <chrono>

namespace nf::engined
{

constexpr int kIpcClientTimeoutMs = 10;
constexpr auto kIpcHealthCheckInterval = std::chrono::seconds(1);

CoreEngine::CoreEngine() : Core("engined")
{
}

bool CoreEngine::onInit()
{
    initConfig();
    initLogger();

    LOG_INFO("CoreEngine init...");

    initThreadManager();

    if (!initIpcClient())
    {
        return false;
    }

    return true;
}

void CoreEngine::initConfig()
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

void CoreEngine::initLogger()
{
    nf::util::Logger::Init(
            m_loggerConfig.name,
            m_loggerConfig.file,
            m_loggerConfig.maxFileSize,
            m_loggerConfig.maxFiles
            );
}

bool CoreEngine::initThreadManager()
{
    m_threadManager = std::make_unique<ThreadManager>();
    if (!m_threadManager)
    {
        LOG_FATAL("ThreadManager initialize failed");
        return false;
    }

    return true;
}

bool CoreEngine::initIpcClient()
{
    m_ipcClient = std::make_unique<IpcClient>(m_ipcConfig, nf::ipc::IpcDaemon::Engined);
    
    if (!m_ipcClient->init())
    {
        LOG_ERROR("IpcClient init failed");
        return false;
    }
    return true;
}

void CoreEngine::onLoop()
{
    LOG_INFO("CoreEngine Runtime Loop Started");

    auto lastHealthCheckAt = std::chrono::steady_clock::now();

    while (!stopping())
    {
        m_ipcClient->poll(kIpcClientTimeoutMs);

        const auto now = std::chrono::steady_clock::now();

        if (now - lastHealthCheckAt >= kIpcHealthCheckInterval)
        {
            processIpcHealthCheck();
            lastHealthCheckAt = now;
        }

        processRuntime();
    }

    LOG_INFO("CoreEngine Runtime Loop Stopped");
}

void CoreEngine::onShutdown()
{
    LOG_INFO("CoreEngine shutdown...");

    m_threadManager->stopAll();

    LOG_INFO("All threads terminated successfully");

    nf::util::Logger::Shutdown();
}

std::uint32_t CoreEngine::nextSeqNo()
{
    return ++m_seqNo;
}

void CoreEngine::processIpcHealthCheck()
{
    std::string name = nf::ipc::IpcProtocol::daemonToStr(
        nf::ipc::IpcDaemon::Engined
    );

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
        nf::ipc::IpcDaemon::Engined,
        nf::ipc::IpcDaemon::Ipcd,
        nf::ipc::IpcCmd::ClientHello,
        nextSeqNo(),
        static_cast<std::uint8_t>(nf::ipc::IpcFlag::Request));

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(
        reinterpret_cast<const std::uint8_t*>(name.data()),
        name.size());

    m_ipcClient->send(std::move(msg));
}

void CoreEngine::processRuntime()
{

}

} // namespace nf::ipcd
