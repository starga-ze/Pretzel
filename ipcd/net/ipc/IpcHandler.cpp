#include "net/ipc/IpcHandler.h"
#include "net/ipc/IpcConnection.h"
#include "net/socket/UnixDomainSocket.h"
#include "io/Epoll.h"
#include "util/Logger.h"

#include <cerrno>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/socket.h>

namespace nf::ipcd
{

IpcHandler::IpcHandler(const nf::config::IpcConfig& cfg)
    : m_cfg(cfg)
{
}

void IpcHandler::handleAccept(
        UnixDomainSocket& listener,
        std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
        nf::io::Epoll& epoll)
{
    while (true)
    {
        const int fd = listener.accept();
        if (fd < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            LOG_ERROR("IpcHandler: accept failed errno={}", errno);
            break;
        }

        if (connections.size() >= static_cast<std::size_t>(m_cfg.maxConnections))
        {
            LOG_WARN("IpcHandler: max connections reached max={} fd={}",
                     m_cfg.maxConnections,
                     fd);
            ::close(fd);
            continue;
        }

        auto conn = std::make_unique<IpcConnection>(
                fd,
                static_cast<std::size_t>(m_cfg.rxBufferSize),
                static_cast<std::size_t>(m_cfg.txBufferSize));
        if (!conn)
        {
            LOG_ERROR("IpcHandler: connection allocation failed fd={}", fd);
            ::close(fd);
            continue;
        }

        if (!epoll.add(fd, EPOLLIN | EPOLLRDHUP))
        {
            LOG_ERROR("IpcHandler: epoll add connection failed fd={}", fd);
            ::close(fd);
            continue;
        }

        connections.emplace(fd, std::move(conn));

        LOG_INFO("IpcHandler: accepted connection fd={} total={}",
                 fd,
                 connections.size());
    }
}

void IpcHandler::handleReadable(
        int fd,
        std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
        nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return;

    auto& conn = *it->second;

    while (true)
    {
        const std::size_t writable = conn.rxWritable();
        if (writable == 0)
        {
            LOG_WARN("IpcHandler: rx buffer full fd={}, closing", fd);
            closeConnection(fd, connections, epoll);
            return;
        }

        const ssize_t n = ::recv(fd, conn.rxWritePtr(), writable, 0);
        if (n > 0)
        {
            conn.rxProduce(static_cast<std::size_t>(n));

            const std::size_t readable = conn.rxReadable();
            LOG_DEBUG("IpcHandler: received {} bytes from fd={}", readable, fd);

            conn.rxConsumeAll();
            continue;
        }

        if (n == 0)
        {
            LOG_INFO("IpcHandler: peer closed fd={}", fd);
            closeConnection(fd, connections, epoll);
            return;
        }

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;

        LOG_ERROR("IpcHandler: recv failed fd={} errno={}", fd, errno);
        closeConnection(fd, connections, epoll);
        return;
    }
}

void IpcHandler::handleWritable(
        int fd,
        std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
        nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return;

    auto& conn = *it->second;

    while (conn.txReadable() > 0)
    {
        const std::uint8_t* data = conn.txReadPtr();
        const std::size_t len = conn.txReadable();

        const ssize_t n = ::send(fd, data, len, MSG_NOSIGNAL);
        if (n > 0)
        {
            conn.txConsume(static_cast<std::size_t>(n));
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (!epoll.mod(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP))
            {
                LOG_ERROR("IpcHandler: epoll mod keep EPOLLOUT failed fd={}", fd);
                closeConnection(fd, connections, epoll);
            }
            return;
        }

        LOG_ERROR("IpcHandler: send failed fd={} errno={}", fd, errno);
        closeConnection(fd, connections, epoll);
        return;
    }

    if (!epoll.mod(fd, EPOLLIN | EPOLLRDHUP))
    {
        LOG_ERROR("IpcHandler: epoll mod remove EPOLLOUT failed fd={}", fd);
        closeConnection(fd, connections, epoll);
    }
}

void IpcHandler::closeConnection(
        int fd,
        std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
        nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return;

    epoll.del(fd);
    connections.erase(it);

    LOG_INFO("IpcHandler: connection removed fd={} total={}",
             fd,
             connections.size());
}

} // namespace nf::ipcd
