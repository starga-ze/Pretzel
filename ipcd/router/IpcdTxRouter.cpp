#include "router/IpcdTxRouter.h"
#include "ipc/IpcServerHandler.h"
#include "util/Logger.h"

namespace pz::ipcd
{

IpcdTxRouter::IpcdTxRouter(IpcServerHandler* ipcServerHandler)
    : m_ipcServerHandler(ipcServerHandler)
{
}

void IpcdTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("message is not initialized");
        return;
    }

    if (!m_ipcServerHandler)
    {
        LOG_ERROR("IpcServerHandler is not initialized");
        return;
    }

    m_ipcServerHandler->egress(std::move(msg));
}

} // namespace pz::ipcd
