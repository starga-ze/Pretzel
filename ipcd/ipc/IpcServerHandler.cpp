#include "ipc/IpcServerHandler.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <cerrno>
#include <unistd.h>

namespace nf::ipcd
{

IpcServerHandler::IpcServerHandler(const nf::config::IpcConfig& cfg)
    : m_cfg(cfg)
{
}

void IpcServerHandler::handleAccept(
    nf::socket::UnixDomainSocket& listener,
    std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
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

            LOG_ERROR("IpcServerHandler: accept failed errno={}", errno);
            break;
        }

        if (connections.size() >= static_cast<std::size_t>(m_cfg.maxConnections))
        {
            LOG_WARN("IpcServerHandler: max connections reached max={} fd={}",
                     m_cfg.maxConnections,
                     fd);
            ::close(fd);
            continue;
        }

        auto conn = std::make_unique<nf::ipc::IpcConnection>(
            fd,
            static_cast<std::size_t>(m_cfg.rxBufferSize),
            static_cast<std::size_t>(m_cfg.txBufferSize));

        if (!conn)
        {
            LOG_ERROR("IpcServerHandler: connection allocation failed fd={}", fd);
            ::close(fd);
            continue;
        }

        if (!epoll.add(fd, EPOLLIN | EPOLLRDHUP))
        {
            LOG_ERROR("IpcServerHandler: epoll add connection failed fd={}", fd);
            ::close(fd);
            continue;
        }

        connections.emplace(fd, std::move(conn));

        LOG_INFO("IpcServerHandler: accepted connection fd={} total={}",
                 fd,
                 connections.size());
    }
}

bool IpcServerHandler::handleRecv(
    int fd,
    std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
    nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return false;

    if (!nf::ipc::IpcHandler::handleRecv(fd, *it->second, epoll))
    {
        closeConnection(fd, connections, epoll);
        return false;
    }

    return true;
}

bool IpcServerHandler::handleSend(
    int fd,
    std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
    nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return false;

    if (!nf::ipc::IpcHandler::handleSend(fd, *it->second, epoll))
    {
        closeConnection(fd, connections, epoll);
        return false;
    }

    return true;
}

void IpcServerHandler::closeConnection(
    int fd,
    std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
    nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return;

    epoll.del(fd);
    connections.erase(it);

    LOG_INFO("IpcServerHandler: connection removed fd={} total={}", fd, connections.size());
}

void IpcServerHandler::onMessage(int fd, const nf::ipc::IpcMessage& msg)
{

}

} // namespace nf::ipcd
