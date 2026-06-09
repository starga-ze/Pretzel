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
        LOG_WARN("EnginedTxRouter: message is nullptr");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("EnginedTxRouter: IpcClientHandler is nullptr");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

} // namespace pz::engined
