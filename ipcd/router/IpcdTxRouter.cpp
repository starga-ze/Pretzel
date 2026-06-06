#include "router/IpcdTxRouter.h"
#include "ipc/IpcServerHandler.h"
#include "util/Logger.h"

namespace nf::ipcd
{

IpcdTxRouter::IpcdTxRouter(IpcServerHandler* ipcServerHandler)
    : m_ipcServerHandler(ipcServerHandler)
{
}

void IpcdTxRouter::handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("IpcdTxRouter: message is nullptr");
        return;
    }

    if (!m_ipcServerHandler)
    {
        LOG_ERROR("IpcdTxRouter: IpcServerHandler is nullptr");
        return;
    }

    m_ipcServerHandler->egress(std::move(msg));
}

} // namespace nf::ipcd
