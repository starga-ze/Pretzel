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
    auto it = connections.find(fd);
    if (it == connections.end())
        return;

    epoll.del(fd);
    connections.erase(it);

    LOG_INFO("IpcServerHandler: connection removed fd={} total={}", fd, connections.size());
}

void IpcServerHandler::onRxMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
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
        LOG_FATAL("Nullptr: ipcServer");
        return;
    }

    const int fd = m_routeTable.findFd(msg->getDst());
    if (fd < 0)
    {
        LOG_WARN("No route for dst={}", nf::ipc::IpcProtocol::daemonToStr(msg->getDst()));
        return;
    }

    LOG_TRACE("IPC Tx Message dump:\n{}", msg->dump());
    
    m_ipcServer->enqueueMessage(fd, std::move(msg));
}

} // namespace nf::ipcd
