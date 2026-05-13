#include "router/EnginedRxRouter.h"
#include "util/Logger.h"

namespace nf::engined
{

EnginedRxRouter::EnginedRxRouter(EnginedTxRouter* txRouter) : m_txRouter(txRouter)
{
}

void EnginedRxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    LOG_DEBUG("Engined Rx Router handle Message");

    if (!m_txRouter)
    {
        LOG_FATAL("TxRouter is nullptr");
        return;
    }
    m_txRouter->handleMessage(std::move(msg));
}

} // namespace nf::engined
