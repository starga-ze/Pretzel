#include "net/ipc/IpcServer.h"
#include "util/Logger.h"

namespace nf::ipcd
{

IpcServer::IpcServer(const nf::config::IpcConfig& cfg)
    : m_cfg(cfg),
      m_handler(cfg),
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

    if (!initListenSocket())
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
        const int n = m_epoll.wait(m_events, -1);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            LOG_WARN("IpcServer: epoll wait failed errno={}", errno);
            continue;
        }

        for (int i = 0; i < n; ++i)
        {
            const int fd = m_events[i].data.fd;
            const std::uint32_t events = m_events[i].events;

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

bool IpcServer::sendTo(int fd, const std::uint8_t* data, std::size_t len)
{
    auto it = m_connections.find(fd);
    if (it == m_connections.end())
        return false;

    auto& conn = *it->second;
    if (!conn.enqueueTx(data, len))
    {
        LOG_WARN("IpcServer: tx buffer full fd={} len={}", fd, len);
        return false;
    }

    if (!m_epoll.mod(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP))
    {
        LOG_ERROR("IpcServer: epoll mod add EPOLLOUT failed fd={}", fd);
        closeConnection(fd);
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
    const bool isRead = (events & EPOLLIN) != 0;
    const bool isWrite = (events & EPOLLOUT) != 0;

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
            LOG_ERROR("Listen fd abnormal events=0x{:x}", events);
            return;
        }

        if (isRead)
            m_handler.handleAccept(*m_listener, m_connections, m_epoll);

        return;
    }

    /* Connection fd */
    auto it = m_connections.find(fd);
    if (it == m_connections.end())
    {
        LOG_WARN("Unknown fd event fd={} events=0x{:x}", fd, events);
        return;
    }

    if (isClose)
    {
        LOG_INFO("Connection close event fd={} events=0x{:x}", fd, events);
        closeConnection(fd);
        return;
    }

    if (isRead)
        m_handler.handleReadable(fd, m_connections, m_epoll);

    if (isWrite)
        m_handler.handleWritable(fd, m_connections, m_epoll);
}

void IpcServer::closeConnection(int fd)
{
    auto it = m_connections.find(fd);
    if (it == m_connections.end())
        return;

    m_epoll.del(fd);
    m_connections.erase(it);

    LOG_INFO("IpcServer: connection removed fd={} total={}", fd, m_connections.size());
}

} // namespace nf::ipcd
