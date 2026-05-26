#include "router/EnginedRxRouter.h"
#include "util/Logger.h"

namespace nf::engined
{

EnginedRxRouter::EnginedRxRouter(EnginedTxRouter* txRouter) : m_txRouter(txRouter)
{
}

void EnginedRxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    LOG_DEBUG("RxRouter handle Message");

    if (!m_txRouter)
    {
        LOG_FATAL("TxRouter is nullptr");
        return;
    }

    switch(msg->getCmd())
    {
        case nf::ipc::IpcCmd::ServerHello:
            m_process->onServerHello(*msg);
            break;

        case nf::ipc::IpcCmd::Sync:
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
