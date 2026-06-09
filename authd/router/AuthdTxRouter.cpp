#include "router/AuthdTxRouter.h"
#include "util/Logger.h"

namespace pz::authd
{

AuthdTxRouter::AuthdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler) :
    m_ipcClientHandler(ipcClientHandler)
{
}

void AuthdTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("Message is not initialized");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("IpcClientHandler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

} // namespace pz::authd
