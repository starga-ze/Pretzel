#include "icmp/IcmpConnection.h"

#include "util/Logger.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <netinet/in.h>
#include <sys/socket.h>

namespace pz::icmpd
{

IcmpConnection::IcmpConnection(int fd,
                               std::size_t rxQueueLimit,
                               std::size_t txQueueLimit)
    : m_fd(fd),
      m_rxQueueLimit(rxQueueLimit),
      m_txQueueLimit(txQueueLimit)
{
}

int IcmpConnection::fd() const noexcept
{
    return m_fd;
}

IcmpIoResult IcmpConnection::recv(int& outErrno)
{
    outErrno = 0;

    if (m_fd < 0)
    {
        outErrno = EBADF;
        return IcmpIoResult::Error;
    }

    std::vector<std::uint8_t> buffer(RECV_BUFFER_SIZE);

    while (true)
    {
        if (m_rxQueue.size() >= m_rxQueueLimit)
            return IcmpIoResult::BufferFull;

        sockaddr_in addr {};
        socklen_t addrLen = sizeof(addr);

        const ssize_t n = ::recvfrom(m_fd,
                                     buffer.data(),
                                     buffer.size(),
                                     0,
                                     reinterpret_cast<sockaddr*>(&addr),
                                     &addrLen);

        if (n > 0)
        {
            char ipBuf[INET_ADDRSTRLEN] {};
            if (!::inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf)))
            {
                outErrno = errno;
                return IcmpIoResult::Error;
            }

            RxFrame frame;
            frame.bytes.assign(buffer.begin(), buffer.begin() + n);
            frame.srcIp = ipBuf;

            m_rxQueue.push(std::move(frame));
            continue;
        }

        if (n == 0)
            return IcmpIoResult::PeerClosed;

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return IcmpIoResult::WouldBlock;

        outErrno = errno;
        return IcmpIoResult::Error;
    }
}

IcmpIoResult IcmpConnection::send(int& outErrno)
{
    outErrno = 0;

    if (m_fd < 0)
    {
        outErrno = EBADF;
        return IcmpIoResult::Error;
    }

    while (!m_txQueue.empty())
    {
        const TxFrame& frame = m_txQueue.front();

        if (frame.bytes.empty())
        {
            m_txQueue.pop();
            continue;
        }

        sockaddr_in addr {};
        addr.sin_family = AF_INET;

        if (::inet_pton(AF_INET, frame.dstIp.c_str(), &addr.sin_addr) != 1)
        {
            LOG_WARN("ICMP Tx drop: invalid dstIp={}", frame.dstIp);
            m_txQueue.pop();
            continue;
        }

        const ssize_t n = ::sendto(m_fd,
                                   frame.bytes.data(),
                                   frame.bytes.size(),
                                   0,
                                   reinterpret_cast<sockaddr*>(&addr),
                                   sizeof(addr));

        if (n > 0)
        {
            if (static_cast<std::size_t>(n) != frame.bytes.size())
            {
                LOG_WARN("ICMP Tx drop: partial send dst={} sent={} expected={}",
                         frame.dstIp,
                         n,
                         frame.bytes.size());

                m_txQueue.pop();
                continue;
            }

            m_txQueue.pop();
            continue;
        }

        if (n == 0)
        {
            LOG_WARN("ICMP Tx drop: sendto returned 0 dst={}", frame.dstIp);
            m_txQueue.pop();
            continue;
        }

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return IcmpIoResult::WouldBlock;

        const int err = errno;

        switch (err)
        {
        case EHOSTUNREACH:
        case ENETUNREACH:
        case EINVAL:
        case EADDRNOTAVAIL:
        case EMSGSIZE:
            LOG_WARN("ICMP Tx drop: dst={} size={} errno={}",
                     frame.dstIp,
                     frame.bytes.size(),
                     err);

            m_txQueue.pop();
            continue;

        default:
            outErrno = err;
            return IcmpIoResult::Error;
        }
    }

    return IcmpIoResult::Ok;
}

bool IcmpConnection::write(std::vector<std::uint8_t> bytes,
                           std::string dstIp)
{
    if (bytes.empty())
        return true;

    if (dstIp.empty())
        return false;

    if (m_txQueue.size() >= m_txQueueLimit)
        return false;

    m_txQueue.push(TxFrame {
        std::move(bytes),
        std::move(dstIp),
    });

    return true;
}

bool IcmpConnection::read(RxFrame& outFrame)
{
    if (m_rxQueue.empty())
        return false;

    outFrame = std::move(m_rxQueue.front());
    m_rxQueue.pop();
    return true;
}

bool IcmpConnection::hasPendingRx() const noexcept
{
    return !m_rxQueue.empty();
}

bool IcmpConnection::hasPendingTx() const noexcept
{
    return !m_txQueue.empty();
}

std::size_t IcmpConnection::rxQueueSize() const noexcept
{
    return m_rxQueue.size();
}

std::size_t IcmpConnection::txQueueSize() const noexcept
{
    return m_txQueue.size();
}

} // namespace pz::icmpd
