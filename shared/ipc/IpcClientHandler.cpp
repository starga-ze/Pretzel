#include "ipc/IpcClientHandler.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace nf::ipc
{

void IpcClientHandler::onMessage(int fd, const IpcMessage& msg)
{
    LOG_INFO("IpcClientHandler: recv fd={} src={} dst={} cmd={} flags={} seq={} payload={}bytes",
             fd,
             IpcProtocol::daemonToStr(msg.src),
             IpcProtocol::daemonToStr(msg.dst),
             IpcProtocol::cmdToStr(msg.cmd),
             IpcProtocol::flagsToStr(msg.flags),
             msg.seqNo,
             msg.payload.size());

    LOG_DEBUG("IpcClientHandler message dump:\n{}", msg.dump());
}

} // namespace nf::ipc
