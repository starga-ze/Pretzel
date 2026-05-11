#include "ipc/IpcClientHandler.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace nf::ipc
{

void IpcClientHandler::onMessage(const IpcMessage& msg)
{
    LOG_DEBUG("IpcClientHandler message dump:\n{}", msg.dump());
}

} // namespace nf::ipc
