#include "ipc/IpcServerHandler.h"

#include "ipc/IpcServer.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <cerrno>
#include <unistd.h>

using namespace nf::ipc;

namespace nf::ipcd
{

IpcServerHandler::IpcServerHandler(IpcServer* ipcServer, const nf::config::IpcConfig& cfg) : 
    m_ipcServer(ipcServer),
    m_rxRouter(std::make_unique<IpcdRxRouter>()),
    m_txRouter(std::make_unique<IpcdTxRouter>(this)),
    m_cfg(cfg)
{
}

void IpcServerHandler::handleAccept(nf::socket::UnixDomainSocket& listener, 
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
            LOG_WARN("IpcServerHandler: max connections reached max={} fd={}", m_cfg.maxConnections, fd);
            ::close(fd);
            continue;
        }

        auto conn = std::make_unique<IpcConnection>(fd, static_cast<std::size_t>(m_cfg.rxBufferSize),
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

        LOG_INFO("IpcServerHandler: accepted connection fd={} total={}", fd, connections.size());
    }
}

bool IpcServerHandler::handleRecv(int fd, 
        std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
        nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return false;

    if (!IpcHandler::handleRecv(fd, *it->second, epoll))
    {
        closeConnection(fd, connections, epoll);
        return false;
    }

    return true;
}

bool IpcServerHandler::handleSend(int fd, 
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

void IpcServerHandler::closeConnection(int fd,
        std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
        nf::io::Epoll& epoll)
{
    removeRoute(fd);

    auto it = connections.find(fd);
    if (it == connections.end())
    {
        return;
    }

    epoll.del(fd);
    connections.erase(it);

    LOG_INFO("Connection removed: fd={}, total={}", fd, connections.size());
}

void IpcServerHandler::onRxMessage(int fd, std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg or !m_rxRouter)
    {
        LOG_WARN("IpcMessage or RxRouter is nullptr");
        return;
    }

    bindRoute(msg->getSrc(), fd);

    LOG_TRACE("IPC Rx Message dump:\n{}", msg->dump());

    m_rxRouter->handleMessage(std::move(msg));
}

void IpcServerHandler::onTxMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        return;
    }

    if (m_ipcServer == nullptr)
    {
        LOG_FATAL("ipcServer is nullptr");
        return;
    }

    const int fd = findRoute(msg->getDst());
    if (fd < 0)
    {
        LOG_WARN("No route for dst={}", nf::ipc::IpcProtocol::daemonToStr(msg->getDst()));
        return;
    }

    LOG_TRACE("IPC Tx Message dump:\n{}", msg->dump());
    
    if (!m_ipcServer->enqueueFrame(fd, std::move(msg)))
    {
        LOG_WARN("Enqueue Tx Failed: fd={}", fd);
    }
}

void IpcServerHandler::bindRoute(nf::ipc::IpcDaemon daemon, int fd)
{
    auto it = m_routeTable.find(daemon);
    if (it == m_routeTable.end())
    {
        m_routeTable.emplace(daemon, fd);

        LOG_DEBUG("Route added: daemon={}, fd={}, routes={}", 
                nf::ipc::IpcProtocol::daemonToStr(daemon),
                fd, m_routeTable.size());
        return;
    }

    if (it->second == fd)
    {
        LOG_TRACE("Route unchanged: daemon={}, fd={}", 
                nf::ipc::IpcProtocol::daemonToStr(daemon), fd);
        return;
    }

    const int oldFd = it->second;
    it->second = fd;

    LOG_WARN("Route updated: daemon={}, oldFd={} -> newFd={}, routes={}",
            nf::ipc::IpcProtocol::daemonToStr(daemon),
            oldFd,
            fd,
            m_routeTable.size());
}

int IpcServerHandler::findRoute(nf::ipc::IpcDaemon daemon) const
{
    auto it = m_routeTable.find(daemon);
    if (it == m_routeTable.end())
    {
        return -1;
    }

    return it->second;
}

void IpcServerHandler::removeRoute(int fd)
{
    for (auto it = m_routeTable.begin(); it != m_routeTable.end();)
    {
        if (it->second == fd)
        {
            LOG_DEBUG("Route removed. daemon={}, fd={}",
                    nf::ipc::IpcProtocol::daemonToStr(it->first),
                    fd);
            it = m_routeTable.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace nf::ipcd
