#include "process/EnginedProcess.h"
#include "util/Logger.h"

#include <chrono>

namespace nf::engined
{

constexpr int kIpcClientTimeoutMs = 10;
constexpr auto kIpcHealthCheckInterval = std::chrono::seconds(1);

EnginedProcess::EnginedProcess(nf::ipc::IpcClient* ipcClient, nf::router::TxRouter* txRouter) : 
    m_ipcClient(ipcClient), 
    m_txRouter(txRouter)
{
}

void EnginedProcess::tick()
{

}

} // namespace nf::engined
