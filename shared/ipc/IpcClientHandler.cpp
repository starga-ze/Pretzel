#include "ipc/IpcClientHandler.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace nf::ipc
{

void IpcClientHandler::onRxMessage(std::unique_ptr<IpcMessage> msg)
{
    LOG_TRACE("IPC Rx Message Dump:\n{}", msg->dump());
    // m_rxRouter->handleMessage(std::move(msg));
}

void IpcClientHandler::onTxMessage(std::unique_ptr<IpcMessage> msg)
{
    LOG_TRACE("IPC Tx Message Dump:\n{}", msg->dump());
}

} // namespace nf::ipc
