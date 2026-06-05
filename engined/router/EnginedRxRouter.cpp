#include "router/EnginedRxRouter.h"
#include "util/Logger.h"

namespace nf::engined
{

EnginedRxRouter::EnginedRxRouter(nf::ipc::IpcClientHandler* ipcClientHandler, EnginedTxRouter* txRouter) : 
    m_ipcClientHandler(ipcClientHandler),
    m_txRouter(txRouter)
{
}

void EnginedRxRouter::handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!m_txRouter or !m_process)
    {
        LOG_ERROR("Dependency is not ready, (txRouter={}, process={})",
                static_cast<bool>(m_txRouter),
                static_cast<bool>(m_process));
        return;
    }

    switch(msg->getCmd())
    {
        case nf::ipc::IpcCmd::ServerHello:
            m_process->onServerHello(*msg);
            break;

        case nf::ipc::IpcCmd::SyncResponse:
            m_process->onSync(*msg);
            break;

        default:
            LOG_WARN("Unhandled Rx msg, cmd={}", static_cast<int>(msg->getCmd()));
            break;
    }
}

void EnginedRxRouter::setProcess(EnginedProcess* process)
{
    m_process = process;
}

} // namespace nf::engined
