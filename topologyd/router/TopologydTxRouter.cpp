#include "ipc/IpcClientHandler.h"
#include "router/TopologydTxRouter.h"
#include "util/Logger.h"

namespace pz::topologyd
{

TopologydTxRouter::TopologydTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler)
    : m_ipcClientHandler(ipcClientHandler)
{
}

void TopologydTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("TopologydTxRouter: message is nullptr");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_FATAL("TopologydTxRouter: IpcClientHandler is nullptr");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

} // namespace pz::topologyd
