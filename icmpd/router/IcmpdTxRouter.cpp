#include "router/IcmpdTxRouter.h"
#include "util/Logger.h"

namespace nf::icmpd
{

IcmpdTxRouter::IcmpdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler) : 
    m_ipcClientHandler(ipcClientHandler)
{
}

void IcmpdTxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
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

void IcmpdTxRouter::handleAction(std::unique_ptr<IcmpdAction> action)
{
    if (!action)
    {
        LOG_WARN("Action is empty");
        return;
    }

    auto msg = action->takeMessage();

    handleMessage(std::move(msg));
}

} // namespace nf::icmpd
