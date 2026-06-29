#include "socket/UdpSocket.h"

#include "util/Logger.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>

namespace pz::socket
{

UdpSocket::~UdpSocket()
{
    close();
}

bool UdpSocket::open()
{
    if (m_fd >= 0)
    {
        LOG_WARN("UdpSocket already opened (fd={})", m_fd);
        return true;
    }

    m_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_fd < 0)
    {
        LOG_ERROR("socket(AF_INET, SOCK_DGRAM) failed (errno={})", errno);
        return false;
    }

    if (!setNonBlocking(m_fd))
    {
        LOG_ERROR("setNonBlocking failed (fd={}, errno={})", m_fd, errno);
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    LOG_DEBUG("UdpSocket ready (fd={})", m_fd);
    return true;
}

void UdpSocket::close()
{
    if (m_fd >= 0)
    {
        ::close(m_fd);
        m_fd = -1;
    }
}

int UdpSocket::fd() const
{
    return m_fd;
}

bool UdpSocket::setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;

    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace pz::socket
