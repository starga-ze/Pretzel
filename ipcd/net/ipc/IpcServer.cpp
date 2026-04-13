#include "net/ipc/IpcServer.h"

#include "util/Logger.h"

#include <cerrno>
#include <unistd.h>

namespace nf::ipcd
{

IpcServer::IpcServer(const nf::config::IpcConfig& cfg)
    : m_cfg(cfg),
      m_events(MAX_EVENTS)
{
}

IpcServer::~IpcServer()
{
    stop();

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

    if (!initListener())
        return false;

    if (!registerListenFd())
        return false;

    m_initialized = true;

    LOG_INFO("IpcServer initialized path={}", m_cfg.socketPath);
    return true;
}

void IpcServer::start()
{
    if (!init())
    {
        LOG_ERROR("IpcServer: init failed");
        return;
    }

    m_running = true;

    LOG_INFO("IpcServer start");

    while (m_running)
    {
        int n = m_epoll.wait(m_events, -1);
        if (n < 0)
        {
            LOG_WARN("IpcServer: epoll wait failed errno={}", errno);
            continue;
        }

        for (int i = 0; i < n; ++i)
        {
            const int fd = m_events[i].data.fd;
            const uint32_t events = m_events[i].events;

            handleEvent(fd, events);
        }
    }

    LOG_INFO("IpcServer stopped");
}

void IpcServer::stop()
{
    m_running = false;
    m_epoll.wakeup();
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

bool IpcServer::initListener()
{
    m_listener = std::make_unique<UnixDomainSocket>(m_cfg.socketPath);
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

    return true;
}

bool IpcServer::registerListenFd()
{
    if (!m_listener)
    {
        LOG_ERROR("IpcServer: listener not initialized");
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

void IpcServer::handleEvent(int fd, uint32_t events)
{
    if (fd == m_epoll.getEventFd())
    {
        m_epoll.drainWakeup();
        return;
    }

    if (m_listener && fd == m_listener->fd())
    {
        handleListenEvent(events);
        return;
    }

    auto it = m_connections.find(fd);
    if (it != m_connections.end())
    {
        if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
        {
            LOG_INFO("IpcServer: connection closed fd={} events=0x{:x}", fd, events);
            closeConnection(fd);
            return;
        }

        LOG_DEBUG("IpcServer: connection event fd={} events=0x{:x}", fd, events);
        return;
    }

    LOG_WARN("IpcServer: unknown fd event fd={} events=0x{:x}", fd, events);
}

void IpcServer::handleListenEvent(uint32_t events)
{
    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
    {
        LOG_ERROR("IpcServer: listen fd abnormal events=0x{:x}", events);
        return;
    }

    if (events & EPOLLIN)
        acceptLoop();
}

void IpcServer::acceptLoop()
{
    while (true)
    {
        int fd = m_listener->accept();
        if (fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            LOG_ERROR("IpcServer: accept failed errno={}", errno);
            break;
        }

        if (m_connections.size() >= static_cast<size_t>(m_cfg.maxConnections))
        {
            LOG_WARN("IpcServer: max connections reached max={} fd={}",
                     m_cfg.maxConnections,
                     fd);
            ::close(fd);
            continue;
        }

        auto conn = std::make_unique<IpcConnection>(fd);
        if (!conn)
        {
            LOG_ERROR("IpcServer: connection allocation failed fd={}", fd);
            ::close(fd);
            continue;
        }

        if (!m_epoll.add(fd, EPOLLIN | EPOLLRDHUP))
        {
            LOG_ERROR("IpcServer: epoll add connection failed fd={}", fd);
            ::close(fd);
            continue;
        }

        m_connections.emplace(fd, std::move(conn));

        LOG_INFO("IpcServer: accepted connection fd={} total={}",
                 fd,
                 m_connections.size());
    }
}

void IpcServer::closeConnection(int fd)
{
    auto it = m_connections.find(fd);
    if (it == m_connections.end())
        return;

    m_epoll.del(fd);
    m_connections.erase(it);

    LOG_INFO("IpcServer: connection removed fd={} total={}",
             fd,
             m_connections.size());
}

} // namespace nf::ipcd
