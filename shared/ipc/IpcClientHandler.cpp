#include "ipc/IpcClientHandler.h"

#include "ipc/IpcClient.h"
#include "util/Logger.h"

#include <cerrno>
#include <cstring>

#include <sys/epoll.h>
#include <sys/socket.h>

namespace pz::ipc
{

IpcClientHandler::IpcClientHandler(IpcClient* ipcClient) : 
    m_ipcClient(ipcClient)
{
}

bool IpcClientHandler::handleConnect(int fd, pz::io::Epoll& epoll)
{
    int soError = 0;
    socklen_t len = sizeof(soError);

    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len) != 0)
    {
        LOG_ERROR("getsockopt(SO_ERROR) failed (fd={}, errno={}, error={})",
                fd, errno, std::strerror(errno));
        return false;
    }

    if (soError != 0)
    {
        LOG_ERROR("connect failed (fd={}, so_error={}, error={})", fd, soError,
                std::strerror(soError));
        return false;
    }

    if (!epoll.mod(fd, EPOLLIN | EPOLLRDHUP))
    {
        LOG_ERROR("epoll mod after connect failed (fd={})", fd);
        return false;
    }

    LOG_INFO("connected (fd={})", fd);
    return true;
}

bool IpcClientHandler::handleRecv(int fd, IpcConnection& conn, pz::io::Epoll& epoll)
{
    if (!IpcHandler::handleRecv(fd, conn, epoll))
    {
        return false;
    }

    return true;
}

bool IpcClientHandler::handleSend(int fd, IpcConnection& conn, pz::io::Epoll& epoll)
{
    if (!IpcHandler::handleSend(fd, conn, epoll))
    {
        return false;
    }

    return true;
}

bool IpcClientHandler::ingress(int fd, pz::ipc::IpcFrameView frame)
{
    if (frame.empty())
    {
        LOG_WARN("ingress frame is empty (fd={})", fd);
        return false;
    }

    std::unique_ptr<IpcMessage> msg;

    const auto rc = m_codec.decode(frame, msg);
    if (rc != IpcDecodeResult::Ok)
    {
        LOG_ERROR("ingress decode failed (fd={}, rc={}, frame_size={})", fd, static_cast<int>(rc),
                  frame.size);
        return false;
    }

    if (!msg)
    {
        LOG_ERROR("ingress decode returned null message (fd={}, frame_size={})", fd, frame.size);
        return false;
    }

    LOG_TRACE("IPC ingress message dump:\n{}", msg->dump());

    m_rxRouter->handleIpcMessage(std::move(msg));

    return true;
}

void IpcClientHandler::egress(std::unique_ptr<IpcMessage> msg)
{
    if (!msg)
    {
        LOG_ERROR("egress message is not initialized");
        return;
    }

    if (!m_ipcClient)
    {
        LOG_ERROR("IpcClient is not initialized");
        return;
    }

    LOG_TRACE("IPC egress message dump:\n{}", msg->dump());

    std::vector<std::uint8_t> frame = m_codec.encode(msg);
    if (frame.empty())
    {
        LOG_WARN("egress encode failed (dst={}, cmd={}, payload_bytes={})",
                pz::ipc::IpcProtocol::daemonToStr(msg->getDst()),
                pz::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
                msg->getPayloadLen());
        return;
    }

    const std::size_t frameSize = frame.size();

    if (!m_ipcClient->enqueueFrame(std::move(frame)))
    {
        LOG_WARN("egress enqueue failed (frame_bytes={})",
                frameSize);
    }
}

void IpcClientHandler::setRxRouter(pz::router::RxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
} 

}// namespace pz::ipc
