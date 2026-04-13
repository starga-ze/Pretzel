#include "net/socket/UnixDomainSocket.h"

#include "util/Logger.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

namespace nf::ipcd
{

namespace
{
constexpr int LISTEN_BACKLOG = 64;
}

UnixDomainSocket::UnixDomainSocket(std::string socketPath)
    : m_socketPath(std::move(socketPath))
{
}

UnixDomainSocket::~UnixDomainSocket()
{
    close();
}

bool UnixDomainSocket::open()
{
    if (m_fd >= 0)
    {
        LOG_WARN("UnixDomainSocket already opened fd={}", m_fd);
        return true;
    }

    ::unlink(m_socketPath.c_str());

    if (!createSocket())
        return false;

    if (!bindSocket())
    {
        close();
        return false;
    }

    if (!listenSocket())
    {
        close();
        return false;
    }

    LOG_INFO("UnixDomainSocket ready path={} fd={}", m_socketPath, m_fd);
    return true;
}

void UnixDomainSocket::close()
{
    if (m_fd >= 0)
    {
        ::close(m_fd);
        m_fd = -1;
    }

    if (!m_socketPath.empty())
        ::unlink(m_socketPath.c_str());
}

int UnixDomainSocket::accept()
{
    if (m_fd < 0)
    {
        errno = EBADF;
        return -1;
    }

    while (true)
    {
        int fd = ::accept(m_fd, nullptr, nullptr);
        if (fd < 0)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (!setNonBlocking(fd))
        {
            const int savedErrno = errno;
            LOG_ERROR("UnixDomainSocket: setNonBlocking accepted fd failed fd={} errno={}",
                      fd,
                      savedErrno);
            ::close(fd);
            errno = savedErrno;
            return -1;
        }

        return fd;
    }
}

int UnixDomainSocket::fd()
{
    return m_fd;
}

const std::string& UnixDomainSocket::socketPath()
{
    return m_socketPath;
}

bool UnixDomainSocket::createSocket()
{
    m_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_fd < 0)
    {
        LOG_ERROR("UnixDomainSocket: socket(AF_UNIX) failed errno={}", errno);
        return false;
    }

    if (!setNonBlocking(m_fd))
    {
        LOG_ERROR("UnixDomainSocket: setNonBlocking listen fd failed fd={} errno={}",
                  m_fd,
                  errno);
        return false;
    }

    return true;
}

bool UnixDomainSocket::setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;

    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool UnixDomainSocket::bindSocket()
{
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;

    if (m_socketPath.size() >= sizeof(addr.sun_path))
    {
        LOG_ERROR("UnixDomainSocket: socket path too long path={}", m_socketPath);
        return false;
    }

    std::strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        LOG_ERROR("UnixDomainSocket: bind failed path={} errno={}", m_socketPath, errno);
        return false;
    }

    return true;
}

bool UnixDomainSocket::listenSocket()
{
    if (::listen(m_fd, LISTEN_BACKLOG) != 0)
    {
        LOG_ERROR("UnixDomainSocket: listen failed fd={} errno={}", m_fd, errno);
        return false;
    }

    return true;
}

} // namespace nf::ipcd
