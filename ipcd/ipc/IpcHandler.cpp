#include "ipc/IpcHandler.h"

#include "util/Logger.h"

#include <cerrno>
#include <unistd.h>

#include <sys/epoll.h>

namespace nf::ipcd
{

IpcHandler::IpcHandler(const nf::config::IpcConfig& cfg) : m_cfg(cfg)
{
}

void IpcHandler::handleAccept(nf::socket::UnixDomainSocket& listener,
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

            LOG_ERROR("IpcHandler: accept failed errno={}", errno);
            break;
        }

        if (connections.size() >= static_cast<std::size_t>(m_cfg.maxConnections))
        {
            LOG_WARN("IpcHandler: max connections reached max={} fd={}", m_cfg.maxConnections, fd);
            ::close(fd);
            continue;
        }

        auto conn = std::make_unique<nf::ipc::IpcConnection>(fd, static_cast<std::size_t>(m_cfg.rxBufferSize),
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

        LOG_INFO("IpcHandler: accepted connection fd={} total={}", fd, connections.size());
    }
}

void IpcHandler::handleRecv(int fd, 
        std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
        nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return;

    auto& conn = *it->second;

    int ioErrno = 0;
    const nf::ipc::IoResult rc = conn.recv(ioErrno);

    switch (rc)
    {
    case nf::ipc::IoResult::Ok:
    case nf::ipc::IoResult::WouldBlock:
        break;

    case nf::ipc::IoResult::PeerClosed:
        LOG_INFO("IpcHandler: peer closed fd={}", fd);
        closeConnection(fd, connections, epoll);
        return;

    case nf::ipc::IoResult::BufferFull:
        LOG_WARN("IpcHandler: rx buffer full fd={}, closing", fd);
        closeConnection(fd, connections, epoll);
        return;

    case nf::ipc::IoResult::Error:
        LOG_ERROR("IpcHandler: readToRx failed fd={} errno={}", fd, ioErrno);
        closeConnection(fd, connections, epoll);
        return;
    }

    auto& rx = conn.rx();
    if (rx.readable() > 0)
    {
        LOG_DEBUG("IpcHandler: received {} bytes from fd={}", rx.readable(), fd);
        rx.consume(rx.readable());
    }
}

void IpcHandler::handleSend(int fd, 
        std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
        nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return;

    auto& conn = *it->second;

    int ioErrno = 0;
    const nf::ipc::IoResult rc = conn.send(ioErrno);

    switch (rc)
    {
    case nf::ipc::IoResult::Ok:
        if (!epoll.mod(fd, EPOLLIN | EPOLLRDHUP))
        {
            LOG_ERROR("IpcHandler: epoll mod remove EPOLLOUT failed fd={}", fd);
            closeConnection(fd, connections, epoll);
        }
        return;

    case nf::ipc::IoResult::WouldBlock:
        if (!epoll.mod(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP))
        {
            LOG_ERROR("IpcHandler: epoll mod keep EPOLLOUT failed fd={}", fd);
            closeConnection(fd, connections, epoll);
        }
        return;

    case nf::ipc::IoResult::PeerClosed:
        LOG_INFO("IpcHandler: peer closed while flushing fd={}", fd);
        closeConnection(fd, connections, epoll);
        return;

    case nf::ipc::IoResult::BufferFull:
        LOG_ERROR("IpcHandler: unexpected BufferFull during flush fd={}", fd);
        closeConnection(fd, connections, epoll);
        return;

    case nf::ipc::IoResult::Error:
        LOG_ERROR("IpcHandler: flushTx failed fd={} errno={}", fd, ioErrno);
        closeConnection(fd, connections, epoll);
        return;
    }
}

void IpcHandler::closeConnection(int fd, 
        std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
        nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return;

    epoll.del(fd);
    connections.erase(it);

    LOG_INFO("IpcHandler: connection removed fd={} total={}", fd, connections.size());
}

} // namespace nf::ipcd
