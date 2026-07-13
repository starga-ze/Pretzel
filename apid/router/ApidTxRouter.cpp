#include "router/ApidTxRouter.h"

#include "util/Logger.h"

namespace pz::apid
{

ApidTxRouter::ApidTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler)
    : m_ipcClientHandler(ipcClientHandler)
{
}

void ApidTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("message is not initialized");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("IPC client handler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

} // namespace pz::apid
