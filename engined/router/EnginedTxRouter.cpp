#include "ipc/IpcClientHandler.h"
#include "router/EnginedTxRouter.h"
#include "util/Logger.h"

namespace nf::engined
{

EnginedTxRouter::EnginedTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler) : 
    m_ipcClientHandler(ipcClientHandler)
{
}

void EnginedTxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    LOG_DEBUG("Engined Tx Router handle Message");

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

} // namespace nf::engined
