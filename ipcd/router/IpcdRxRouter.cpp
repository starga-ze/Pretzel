#include "router/IpcdRxRouter.h"
#include "util/Logger.h"

namespace nf::ipcd
{

IpcdRxRouter::IpcdRxRouter(IpcdTxRouter* txRouter) : m_txRouter(txRouter)
{
}

void IpcdRxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    LOG_DEBUG("Ipcd Rx Router handle Message");

    if (!m_txRouter)
    {
        LOG_FATAL("TxRouter is nullptr");
        return;
    }
    m_txRouter->handleMessage(std::move(msg));
}

} // namespace nf::ipcd
