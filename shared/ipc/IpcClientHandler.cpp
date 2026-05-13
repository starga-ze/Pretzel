#include "ipc/IpcClientHandler.h"

#include "util/Logger.h"

#include <cerrno>
#include <cstring>

#include <sys/epoll.h>
#include <sys/socket.h>

namespace nf::ipc
{

bool IpcClientHandler::handleConnect(int fd, nf::io::Epoll& epoll)
{
    int soError = 0;
    socklen_t len = sizeof(soError);

    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len) != 0)
    {
        LOG_ERROR("IpcClientHandler: getsockopt(SO_ERROR) failed fd={} errno={} ({})",
                  fd,
                  errno,
                  std::strerror(errno));
        return false;
    }

    if (soError != 0)
    {
        LOG_ERROR("IpcClientHandler: connect failed fd={} so_error={} ({})",
                  fd,
                  soError,
                  std::strerror(soError));
        return false;
    }

    if (!epoll.mod(fd, EPOLLIN | EPOLLRDHUP))
    {
        LOG_ERROR("IpcClientHandler: epoll mod after connect failed fd={}", fd);
        return false;
    }

    LOG_INFO("IpcClientHandler: connected fd={}", fd);
    return true;
}

bool IpcClientHandler::handleRecv(
    int fd,
    IpcConnection& conn,
    nf::io::Epoll& epoll)
{
    if (!IpcHandler::handleRecv(fd, conn, epoll))
        return false;

    return true;
}

bool IpcClientHandler::handleSend(
    int fd,
    IpcConnection& conn,
    nf::io::Epoll& epoll)
{
    if (!IpcHandler::handleSend(fd, conn, epoll))
        return false;

    return true;
}

bool IpcClientHandler::ingress(int fd, nf::ipc::IpcFrameView frame)
{
    if (frame.empty())
    {
        LOG_WARN("IpcClientHandler: ingress frame is empty fd={}", fd);
        return false;
    }

    std::unique_ptr<IpcMessage> msg;

    const auto rc = m_codec.decode(frame, msg);
    if (rc != IpcDecodeResult::Ok)
    {
        LOG_ERROR("IpcClientHandler: ingress decode failed fd={} rc={} frameSize={}",
                  fd,
                  static_cast<int>(rc),
                  frame.size);
        return false;
    }

    if (!msg)
    {
        LOG_ERROR("IpcClientHandler: ingress decode returned null message fd={} frameSize={}",
                  fd,
                  frame.size);
        return false;
    }

    LOG_TRACE("IPC Client Ingress Message dump:\n{}", msg->dump());

    return true;
}

void IpcClientHandler::egress(std::unique_ptr<IpcMessage> msg)
{
    (void) msg;

    LOG_WARN("Egress is not supported");
}

} // namespace nf::ipc
