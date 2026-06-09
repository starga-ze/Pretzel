#include "router/SnmpdTxRouter.h"
#include "util/Logger.h"

namespace pz::snmpd
{

SnmpdTxRouter::SnmpdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler) :
    m_ipcClientHandler(ipcClientHandler)
{
}

void SnmpdTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_ERROR("Message is not initialized");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("IpcClientHandler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

} // namespace pz::snmpd
