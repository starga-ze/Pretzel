#include "socket/IcmpSocket.h"

#include "util/Logger.h"

#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace pz::socket
{

IcmpSocket::~IcmpSocket()
{
    close();
}

bool IcmpSocket::open()
{
    if (m_fd >= 0)
    {
        LOG_WARN("IcmpSocket already opened (fd={})", m_fd);
        return true;
    }

    if (!createSocket())
        return false;

    LOG_INFO("IcmpSocket ready (fd={})", m_fd);
    return true;
}

void IcmpSocket::close()
{
    if (m_fd >= 0)
    {
        ::close(m_fd);
        m_fd = -1;
    }
}

int IcmpSocket::fd() const
{
    return m_fd;
}

bool IcmpSocket::createSocket()
{
    m_fd = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (m_fd < 0)
    {
        LOG_ERROR("socket(AF_INET, SOCK_RAW, IPPROTO_ICMP) failed (errno={})", errno);
        return false;
    }

    if (!setNonBlocking(m_fd))
    {
        const int savedErrno = errno;

        LOG_ERROR("setNonBlocking failed (fd={}, errno={})", m_fd, savedErrno);

        ::close(m_fd);
        m_fd = -1;
        errno = savedErrno;
        return false;
    }

    return true;
}

bool IcmpSocket::setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;

    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}
