#include "icmp/IcmpConnection.h"

#include <arpa/inet.h>

#include <cerrno>
#include <cstdint>

#include <netinet/in.h>

#include <sys/socket.h>

namespace nf::icmpd
{

namespace
{

constexpr std::size_t kRecvBufferSize = 65536;

} // namespace

IcmpConnection::IcmpConnection(int fd)
    : m_fd(fd)
{
}

IcmpIoResult IcmpConnection::recv(std::vector<std::uint8_t>& outBytes,
                                  std::string& outSrcIp,
                                  int& outErrno)
{
    outBytes.clear();
    outSrcIp.clear();
    outErrno = 0;

    if (m_fd < 0)
    {
        outErrno = EBADF;
        return IcmpIoResult::Error;
    }

    std::vector<std::uint8_t> buffer(kRecvBufferSize);

    while (true)
    {
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

            outBytes.assign(buffer.begin(), buffer.begin() + n);
            outSrcIp = ipBuf;
            return IcmpIoResult::Ok;
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

IcmpIoResult IcmpConnection::send(const std::vector<std::uint8_t>& bytes,
                                  const std::string& dstIp,
                                  int& outErrno)
{
    outErrno = 0;

    if (m_fd < 0)
    {
        outErrno = EBADF;
        return IcmpIoResult::Error;
    }

    if (bytes.empty())
        return IcmpIoResult::Ok;

    sockaddr_in addr {};
    addr.sin_family = AF_INET;

    if (::inet_pton(AF_INET, dstIp.c_str(), &addr.sin_addr) != 1)
    {
        outErrno = EINVAL;
        return IcmpIoResult::Error;
    }

    while (true)
    {
        const ssize_t n = ::sendto(m_fd,
                                   bytes.data(),
                                   bytes.size(),
                                   0,
                                   reinterpret_cast<sockaddr*>(&addr),
                                   sizeof(addr));

        if (n > 0)
        {
            if (static_cast<std::size_t>(n) == bytes.size())
                return IcmpIoResult::Ok;

            return IcmpIoResult::WouldBlock;
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

int IcmpConnection::fd() const
{
    return m_fd;
}

} // namespace nf::icmpd
