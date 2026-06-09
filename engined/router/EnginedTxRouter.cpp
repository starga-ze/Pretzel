#include "ipc/IpcClientHandler.h"
#include "router/EnginedTxRouter.h"
#include "util/Logger.h"

namespace pz::engined
{

EnginedTxRouter::EnginedTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler)
    : m_ipcClientHandler(ipcClientHandler)
{
}

void EnginedTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("message is not initialized");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("Engined TxRouter: IPC client handler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

} // namespace pz::engined
