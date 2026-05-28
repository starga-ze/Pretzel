#include "process/IpcdProcess.h"
#include "util/Logger.h"

namespace nf::ipcd
{

constexpr int kIpcServerTimeoutMs = 10;

IpcdProcess::IpcdProcess(IpcServer* ipcServer, IpcdTxRouter* txRouter) : 
    m_ipcServer(ipcServer), 
    m_txRouter(txRouter)
{
}

bool IpcdProcess::start()
{
    if (!m_ipcServer)
    {
        LOG_ERROR("IpcServer is nullptr");
        return false;
    }

    if (!m_txRouter)
    {
        LOG_ERROR("TxRouter is nullptr");
        return false;
    }

    return true;
}

void IpcdProcess::tick()
{
    m_ipcServer->poll(kIpcServerTimeoutMs);

    processRuntime();
}

void IpcdProcess::processRuntime()
{

}

} // namespace nf::ipcd
