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

    LOG_TRACE("[RX Buffer State] fd={} buffer: used={}/{} ({:.2f}%) free={}", fd, 
            rx.readable(), rx.capacity(),
            rx.capacity() > 0 ? 
            (static_cast<double>(rx.readable()) * 100.0 / static_cast<double>(rx.capacity())) 
            : 0.0,
            rx.writable());

    while (true)
    {
        if (rx.readable() < sizeof(IpcWireHeader))
            return true;

        std::uint8_t headerBuf[sizeof(IpcWireHeader)]{};
        if (rx.peek(headerBuf, sizeof(headerBuf)) < sizeof(headerBuf))
        {
            LOG_TRACE("IpcHandler: incomplete header fd={} readable={} headerLen={}", fd, rx.readable(),
                      sizeof(IpcWireHeader));
            return true;
        }

        std::size_t frameSize = 0;
        const IpcPeekResult peekRc = m_codec.peekFrameSize(headerBuf, sizeof(headerBuf), frameSize);

        if (peekRc == IpcPeekResult::NeedMoreData)
            return true;

        if (peekRc == IpcPeekResult::InvalidFrame)
        {
            LOG_ERROR("IpcHandler: invalid frame header fd={} readable={}", fd, rx.readable());
            return false;
        }

        if (frameSize == 0 || frameSize > IPC_MAX_FRAME_SIZE)
        {
            LOG_ERROR("IpcHandler: invalid frame size fd={} frameSize={} maxFrameSize={}", fd, frameSize,
                      IPC_MAX_FRAME_SIZE);
            return false;
        }

        if (rx.readable() < frameSize)
        {
            LOG_TRACE("IpcHandler: incomplete frame fd={} readable={} frameSize={}", fd, 
                    rx.readable(), frameSize);
            return true;
        }

        std::unique_ptr<IpcMessage> msg;
        IpcDecodeResult rc = IpcDecodeResult::InvalidFrame;

        if (rx.readLen() >= frameSize)
        {
            rc = m_codec.decode(rx.readPtr(), frameSize, msg);
        }
        else
        {
            std::vector<std::uint8_t> frame(frameSize);

            if (rx.peek(frame.data(), frameSize) < frameSize)
            {
                LOG_ERROR("IpcHandler: failed to peek wrapped frame fd={} readable={} frameSize={}", fd, 
                        rx.readable(), frameSize);
                return false;
            }

            LOG_TRACE("IpcHandler: decoding wrapped frame fd={} contiguous={} frameSize={}", fd, 
                    rx.readLen(), frameSize);

            rc = m_codec.decode(frame.data(), frame.size(), msg);
        }

        if (rc == IpcDecodeResult::NeedMoreData)
            return true;

        if (rc != IpcDecodeResult::Ok)
        {
            LOG_ERROR("IpcHandler: decode failed fd={} rc={} frameSize={}", fd, static_cast<int>(rc), 
                    frameSize);
            return false;
        }

        rx.consume(frameSize);

        LOG_TRACE("[RX Buffer State] fd={} consumed={} remaining={} free={}", fd, frameSize, 
                rx.readable(), rx.writable());

        onRxMessage(fd, std::move(msg));
    }
}

} // namespace nf::ipc
