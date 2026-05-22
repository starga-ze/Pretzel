#include "process/EnginedProcess.h"
#include "util/Logger.h"

namespace nf::engined
{

constexpr int kIpcClientTimeoutMs = 10;
constexpr auto kIpcHealthCheckInterval = std::chrono::seconds(1);

EnginedProcess::EnginedProcess(nf::ipc::IpcClient* ipcClient, nf::router::TxRouter* txRouter) : 
    m_ipcClient(ipcClient), 
    m_txRouter(txRouter)
{
}

void EnginedProcess::start()
{
    m_lastHealthCheckAt = std::chrono::steady_clock::now();
    return;
}

void EnginedProcess::tick()
{
    m_ipcClient->poll(kIpcClientTimeoutMs);

    const auto now = std::chrono::steady_clock::now();

    if (now - m_lastHealthCheckAt >= kIpcHealthCheckInterval)
    {
        processIpcHealthCheck();
        m_lastHealthCheckAt = now;
    }

    processRuntime();
}

std::uint32_t EnginedProcess::nextSeqNo()
{
    return ++m_seqNo;
}

void EnginedProcess::processIpcHealthCheck()
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

    m_txRouter->handleMessage(std::move(msg));
}

void EnginedProcess::processRuntime()
{

}

} // namespace nf::engined
