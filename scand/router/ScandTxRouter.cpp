#include "router/ScandTxRouter.h"
#include "util/Logger.h"

namespace pz::scand
{

ScandTxRouter::ScandTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler) : m_ipcClientHandler(ipcClientHandler)
{
}

void ScandTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_ERROR("message is nullptr");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("IpcClientHandler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

}
