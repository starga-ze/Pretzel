#include "router/IcmpdTxRouter.h"
#include "util/Logger.h"

namespace nf::icmpd
{

IcmpdTxRouter::IcmpdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler) : 
    m_ipcClientHandler(ipcClientHandler)
{
}

void IcmpdTxRouter::handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("Message is nullptr");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_FATAL("IpcClientHandler is nullptr");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

void IcmpdTxRouter::handleIcmpPacket(std::unique_ptr<IcmpPacket> packet)
{
    LOG_DEBUG("handle icmp packet");
}

} // namespace nf::icmpd
