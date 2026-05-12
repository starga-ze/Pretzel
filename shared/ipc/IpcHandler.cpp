#include "ipc/IpcHandler.h"

#include "util/Logger.h"

#include <cstring>
#include <vector>

#include <sys/epoll.h>

namespace nf::ipc
{

bool IpcHandler::handleRecv(int fd, IpcConnection& conn, nf::io::Epoll& epoll)
{
    (void)epoll;

    int ioErrno = 0;
    const IoResult rc = conn.recv(ioErrno);

    switch (rc)
    {
    case IoResult::Ok:
    case IoResult::WouldBlock:
        break;

    case IoResult::PeerClosed:
        LOG_INFO("IpcHandler: peer closed fd={}", fd);
        return false;

    case IoResult::BufferFull:
        LOG_WARN("IpcHandler: rx buffer full fd={}, closing", fd);
        return false;

    case IoResult::Error:
        LOG_ERROR("IpcHandler: recv failed fd={} errno={}", fd, ioErrno);
        return false;
    }

    return drainRxFrames(fd, conn);
}

bool IpcHandler::handleSend(int fd, IpcConnection& conn, nf::io::Epoll& epoll)
{
    int ioErrno = 0;
    const IoResult rc = conn.send(ioErrno);

    switch (rc)
    {
    case IoResult::Ok:
        if (!epoll.mod(fd, EPOLLIN | EPOLLRDHUP))
        {
            LOG_ERROR("IpcHandler: epoll mod remove EPOLLOUT failed fd={}", fd);
            return false;
        }
        return true;

    case IoResult::WouldBlock:
        if (!epoll.mod(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP))
        {
            LOG_ERROR("IpcHandler: epoll mod keep EPOLLOUT failed fd={}", fd);
            return false;
        }
        return true;

    case IoResult::PeerClosed:
        LOG_INFO("IpcHandler: peer closed while flushing fd={}", fd);
        return false;

    case IoResult::BufferFull:
        LOG_ERROR("IpcHandler: unexpected BufferFull during flush fd={}", fd);
        return false;

    case IoResult::Error:
        LOG_ERROR("IpcHandler: flushTx failed fd={} errno={}", fd, ioErrno);
        return false;
    }

    return false;
}

bool IpcHandler::drainRxFrames(int fd, IpcConnection& conn)
{
    auto& rx = conn.rx();

    while (true)
    {
        if (rx.readable() < sizeof(IpcWireHeader))
            return true;

        const std::uint8_t* data = rx.readPtr();
        const std::size_t readable = rx.readLen();

        std::size_t frameSize = 0;
        const IpcPeekResult peekRc = m_codec.peekFrameSize(data, readable, frameSize);

        if (peekRc == IpcPeekResult::NeedMoreData)
            return true;

        if (peekRc == IpcPeekResult::InvalidFrame)
        {
            LOG_ERROR("IpcHandler: invalid frame header fd={}", fd);
            return false;
        }

        if (frameSize == 0 || frameSize > IPC_MAX_FRAME_SIZE)
        {
            LOG_ERROR("IpcHandler: invalid frame size={} fd={}", frameSize, fd);
            return false;
        }

        if (readable < frameSize)
            return true;

        std::unique_ptr<IpcMessage> msg;

        const IpcDecodeResult rc = m_codec.decode(data, frameSize, msg);

        if (rc == IpcDecodeResult::NeedMoreData)
        {
            return true;
        }

        if (rc != IpcDecodeResult::Ok)
        {
            LOG_ERROR("IpcHandler: decode failed fd={} rc={} frameSize={}",
                      fd, static_cast<int>(rc), frameSize);
            return false;
        }

        rx.consume(frameSize);

        onRxMessage(fd, std::move(msg));
    }
}

} // namespace nf::ipc
