#include "ipc/IpcConnection.h"
#include "ipc/IpcProtocol.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>

#include <sys/socket.h>
#include <unistd.h>

namespace pz::ipc
{

// A frame is only dispatched once it fits whole in the ring buffer (see
// IpcHandler::drainRxFrames) and is only enqueued if it fits whole on the tx side,
// so each buffer MUST be able to hold one maximum-size frame. Operator-supplied
// sizes (from the running-config) are a floor-clamped hint, never allowed below
// IPC_MAX_FRAME_SIZE — otherwise a large but legal frame stalls forever / fails to
// enqueue even though the codec accepted it.
IpcConnection::IpcConnection(int fd, std::size_t rxBuf, std::size_t txBuf)
    : m_fd(fd),
      m_rx(std::max(rxBuf, IPC_MAX_FRAME_SIZE)),
      m_tx(std::max(txBuf, IPC_MAX_FRAME_SIZE))
{
}

IoResult IpcConnection::recv(int& outErrno)
{
    outErrno = 0;

    while (true)
    {
        std::uint8_t* wptr = m_rx.writePtr();
        const std::size_t wlen = m_rx.writeLen();

        if (wlen == 0)
            return IoResult::BufferFull;

        const ssize_t n = ::recv(m_fd, wptr, wlen, 0);

        if (n > 0)
        {
            m_rx.produce(static_cast<std::size_t>(n));
            continue;
        }

        if (n == 0)
            return IoResult::PeerClosed;

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return IoResult::WouldBlock;

        outErrno = errno;
        return IoResult::Error;
    }
}

IoResult IpcConnection::send(int& outErrno)
{
    outErrno = 0;

    while (m_tx.readable() > 0)
    {
        const std::uint8_t* rptr = m_tx.readPtr();
        const std::size_t rlen = m_tx.readLen();

        const ssize_t n = ::send(m_fd, rptr, rlen, MSG_NOSIGNAL);

        if (n > 0)
        {
            m_tx.consume(static_cast<std::size_t>(n));

            if (static_cast<std::size_t>(n) < rlen)
                return IoResult::WouldBlock;

            continue;
        }

        if (n == 0)
            return IoResult::PeerClosed;

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return IoResult::WouldBlock;

        outErrno = errno;
        return IoResult::Error;
    }

    return IoResult::Ok;
}

bool IpcConnection::write(const std::uint8_t* data, std::size_t len)
{
    if (len == 0)
        return true;

    if (m_tx.writable() < len)
        return false;

    m_tx.write(data, len);
    return true;
}

bool IpcConnection::write(const std::vector<std::uint8_t>& data)
{
    if (data.empty())
        return true;

    return write(data.data(), data.size());
}

} // namespace pz::ipc
