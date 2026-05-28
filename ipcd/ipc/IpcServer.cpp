#include "ipc/IpcServer.h"

#include "util/Logger.h"

#include <cerrno>

#include <sys/epoll.h>

namespace nf::ipcd
{

IpcServer::IpcServer(const nf::config::IpcConfig& cfg, nf::ipc::IpcDaemon selfId) : 
    m_cfg(cfg), 
    m_selfId(selfId), 
    m_events(MAX_EVENTS), 
    m_handler(std::make_unique<IpcServerHandler>(this, cfg))
{
}

IpcServer::~IpcServer()
{
    for (auto& [fd, _] : m_connections)
        m_epoll.del(fd);

    m_connections.clear();

    if (m_listener && m_listener->fd() >= 0)
        m_epoll.del(m_listener->fd());
}

bool IpcServer::init()
{
    if (m_initialized)
        return true;

    if (!initEpoll())
        return false;

    if (!initListenSocket())
        return false;

    m_initialized = true;

    LOG_INFO("IpcServer initialized path={}, self={}", m_cfg.socketPath, 
            nf::ipc::IpcProtocol::daemonToStr(m_selfId));
    return true;
}

bool IpcServer::poll(int timeoutMs)
{
    const int n = m_epoll.wait(m_events, timeoutMs);
    if (n < 0)
    {
        if (errno == EINTR)
        {
            return true;
        }

        LOG_WARN("IpcServer: epoll wait failed errno={}", errno);
        return false;
    }

    for (int i = 0; i < n; ++i)
    {
        const int fd = m_events[i].data.fd;
        const std::uint32_t events = m_events[i].events;

        handleEvent(fd, events);
    }
    
    return true;
}

bool IpcServer::enqueueFrame(int fd, std::vector<std::uint8_t> frame)
{
    auto it = m_connections.find(fd);
    if (it == m_connections.end())
    {
        LOG_WARN("Send rejected, not connected");
        return false;
    }

    if (frame.empty())
    {
        LOG_WARN("Frame is empty");
        return false;
    }

    auto& conn = *it->second;

    if (!conn.write(frame))
    {
        LOG_WARN("IpcServer: tx buffer full fd={} frame={}bytes",
                 fd, frame.size());
        return false;
    }

    if (!m_epoll.mod(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP))
    {
        LOG_ERROR("IpcServer: epoll mod add EPOLLOUT failed fd={}", fd);
        m_handler->closeConnection(fd, m_connections, m_epoll);
        return false;
    }

    return true;
}

bool IpcServer::initEpoll()
{
    if (!m_epoll.init())
    {
        LOG_ERROR("IpcServer: epoll init failed");
        return false;
    }

    return true;
}

bool IpcServer::initListenSocket()
{
    m_listener = std::make_unique<nf::socket::UnixDomainSocket>(m_cfg.socketPath);
    if (!m_listener)
    {
        LOG_ERROR("IpcServer: listener allocation failed");
        return false;
    }

    if (!m_listener->open())
    {
        LOG_ERROR("IpcServer: listener open failed path={}", m_cfg.socketPath);
        return false;
    }

    if (!m_epoll.add(m_listener->fd(), EPOLLIN | EPOLLRDHUP))
    {
        LOG_ERROR("IpcServer: epoll add listen fd failed fd={}", m_listener->fd());
        return false;
    }

    LOG_INFO("IpcServer: listen fd registered fd={}", m_listener->fd());
    return true;
}

void IpcServer::handleEvent(int fd, std::uint32_t events)
{
    const bool isClose = (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
    const bool isRecv = (events & EPOLLIN) != 0;
    const bool isSend = (events & EPOLLOUT) != 0;

    /* Event fd */
    if (fd == m_epoll.getEventFd())
    {
        m_epoll.drainWakeup();
        return;
    }

    /* Listen fd */
    if (m_listener && fd == m_listener->fd())
    {
        if (isClose)
        {
            LOG_ERROR("IpcServer: listen fd abnormal events=0x{:x}", events);
            return;
        }

        if (isRecv)
            m_handler->handleAccept(*m_listener, m_connections, m_epoll);

        return;
    }

    /* Connection fd */
    auto it = m_connections.find(fd);
    if (it == m_connections.end())
    {
        LOG_WARN("IpcServer: unknown fd event fd={} events=0x{:x}", fd, events);
        return;
    }

    if (isClose)
    {
        LOG_INFO("IpcServer: connection close event fd={} events=0x{:x}", fd, events);
        m_handler->closeConnection(fd, m_connections, m_epoll);
        return;
    }

    if (isRecv)
        m_handler->handleRecv(fd, m_connections, m_epoll);

    if (isSend)
        m_handler->handleSend(fd, m_connections, m_epoll);
}

IpcServerHandler* IpcServer::handler()
{
    if (!m_handler)
    {
        LOG_ERROR("IpcServerHandler is nullptr");
        return nullptr;
    }
    return m_handler.get();
}

} // namespace nf::ipcd
