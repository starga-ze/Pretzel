// router/MgmtdTxRouter.cpp
#include "router/MgmtdTxRouter.h"

#include "util/Logger.h"

namespace nf::mgmtd
{

MgmtdTxRouter::MgmtdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler)
    : m_ipcClientHandler(ipcClientHandler)
{
}

void MgmtdTxRouter::handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("MgmtdTxRouter: message is nullptr");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_FATAL("MgmtdTxRouter: IpcClientHandler is nullptr");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

} // namespace nf::mgmtd
