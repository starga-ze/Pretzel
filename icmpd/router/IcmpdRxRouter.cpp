#include "router/IcmpdRxRouter.h"
#include "util/Logger.h"

namespace nf::icmpd
{

IcmpdRxRouter::IcmpdRxRouter(nf::ipc::IpcClientHandler* ipcClientHandler, IcmpdTxRouter* txRouter) : 
    m_ipcClientHandler(ipcClientHandler), 
    m_txRouter(txRouter)
{
}

void IcmpdRxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!m_txRouter or !m_process)
    {
        LOG_ERROR("Dependency is not ready, (txRouter={}, process={})", static_cast<bool>(m_txRouter),
                  static_cast<bool>(m_process));
        return;
    }

    switch (msg->getCmd())
    {
    case nf::ipc::IpcCmd::ServerHello:
        m_process->onServerHello(*msg);
        break;

    case nf::ipc::IpcCmd::RuntimeStart:
        m_process->onRuntimeStart(*msg);
        break;

    default:
        LOG_WARN("Unhandled Rx msg, cmd={}", static_cast<int>(msg->getCmd()));
        break;
    }
}

void IcmpdRxRouter::setProcess(IcmpdProcess* process)
{
    m_process = process;
}

} // namespace nf::icmpd
