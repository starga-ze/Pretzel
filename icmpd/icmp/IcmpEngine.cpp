#include "icmp/IcmpEngine.h"

#include "util/Logger.h"

#include <cerrno>

#include <sys/epoll.h>

namespace pz::icmpd
{

IcmpEngine::IcmpEngine()
    : m_events(MAX_EVENTS),
      m_handler(std::make_unique<IcmpEngineHandler>(this))
{
}

IcmpEngine::~IcmpEngine()
{
    closeConnection();
}

bool IcmpEngine::init()
{
    if (m_initialized)
        return true;

    if (!initEpoll())
        return false;

    if (!initSocket())
        return false;

    if (!initConnection())
        return false;

    if (!m_epoll.add(m_socket->fd(), EPOLLIN | EPOLLRDHUP))
    {
        LOG_ERROR("epoll add failed fd={}", m_socket->fd());
        closeConnection();
        return false;
    }

    m_initialized = true;

    LOG_INFO("IcmpEngine initialized fd={}", m_socket->fd());
    return true;
}

bool IcmpEngine::poll(int timeoutMs)
{
    // Self-heal: if a prior fatal recv/send error tore the connection down
    // (closeConnection), rebuild the socket here instead of staying dead until a
    // daemon restart. Rate-limited by REOPEN_BACKOFF.
    if (!isOpened())
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_nextReopenAt)
            return true;
        m_nextReopenAt = now + REOPEN_BACKOFF;

        if (!reopen())
        {
            LOG_WARN("IcmpEngine reopen failed, will retry");
            return true;
        }
    }

    const int n = m_epoll.wait(m_events, timeoutMs);
    if (n < 0)
    {
        if (errno == EINTR)
            return true;

        LOG_WARN("epoll wait failed errno={}", errno);
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

bool IcmpEngine::sendPacket(std::unique_ptr<IcmpPacket> packet,
                            std::string dstIp)
{
    if (!m_handler)
    {
        LOG_ERROR("IcmpEngineHandler is not initialized");
        return false;
    }

    m_handler->egress(std::move(packet), std::move(dstIp));
    return true;
}

bool IcmpEngine::enqueueFrame(std::vector<std::uint8_t> frame,
                              std::string dstIp)
{
    if (frame.empty())
    {
        LOG_WARN("enqueue rejected, frame is empty dst={}", dstIp);
        return false;
    }

    if (!m_socket || !m_conn || m_socket->fd() < 0)
    {
        LOG_WARN("enqueue rejected, socket is not opened dst={}", dstIp);
        return false;
    }

    if (!m_conn->write(std::move(frame), std::move(dstIp)))
    {
        LOG_WARN("ICMP engine: tx queue full or invalid frame");
        return false;
    }

    if (!m_epoll.mod(m_socket->fd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP))
    {
        LOG_ERROR("epoll mod add EPOLLOUT failed fd={}", m_socket->fd());
        closeConnection();
        return false;
    }

    return true;
}

bool IcmpEngine::isOpened() const
{
    return m_socket && m_socket->fd() >= 0 && m_conn != nullptr;
}

int IcmpEngine::fd() const
{
    return m_socket ? m_socket->fd() : -1;
}

IcmpEngineHandler* IcmpEngine::handler()
{
    if (!m_handler)
    {
        LOG_ERROR("IcmpEngineHandler is not initialized");
        return nullptr;
    }

    return m_handler.get();
}

bool IcmpEngine::initEpoll()
{
    if (!m_epoll.init())
    {
        LOG_ERROR("ICMP engine: epoll init failed");
        return false;
    }

    return true;
}

bool IcmpEngine::initSocket()
{
    if (m_socket)
        return true;

    m_socket = std::make_unique<pz::socket::IcmpSocket>();
    if (!m_socket)
    {
        LOG_ERROR("ICMP engine: socket allocation failed");
        return false;
    }

    if (!m_socket->open())
    {
        LOG_ERROR("ICMP engine: socket open failed");
        m_socket.reset();
        return false;
    }

    return true;
}

bool IcmpEngine::initConnection()
{
    if (m_conn)
        return true;

    if (!m_socket || m_socket->fd() < 0)
    {
        LOG_ERROR("ICMP engine: connection init failed, socket is not opened");
        return false;
    }

    m_conn = std::make_unique<IcmpConnection>(m_socket->fd());
    if (!m_conn)
    {
        LOG_ERROR("ICMP engine: connection allocation failed");
        return false;
    }

    return true;
}

bool IcmpEngine::reopen()
{
    // Rebuild the socket + connection on the existing epoll instance (m_epoll
    // survives a connection drop — only the socket fd was removed from it). This
    // is the recovery counterpart to closeConnection(); epoll is NOT re-init'd
    // here because Epoll::init() is not idempotent.
    closeConnection();

    if (!initSocket())
        return false;

    if (!initConnection())
        return false;

    if (!m_epoll.add(m_socket->fd(), EPOLLIN | EPOLLRDHUP))
    {
        LOG_ERROR("epoll add failed on reopen fd={}", m_socket->fd());
        closeConnection();
        return false;
    }

    m_initialized = true;
    LOG_INFO("IcmpEngine reopened fd={}", m_socket->fd());
    return true;
}

void IcmpEngine::closeConnection()
{
    if (m_socket && m_socket->fd() >= 0)
        m_epoll.del(m_socket->fd());

    m_conn.reset();

    if (m_socket)
        m_socket->close();

    // Drop the socket object too so initSocket() rebuilds it on the next reopen()
    // — it short-circuits on a non-null m_socket, so a closed-but-present socket
    // would otherwise block recovery.
    m_socket.reset();

    m_initialized = false;
}

void IcmpEngine::handleEvent(int fd, std::uint32_t events)
{
    const bool isClose = (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
    const bool isRecv  = (events & EPOLLIN) != 0;
    const bool isSend  = (events & EPOLLOUT) != 0;

    if (fd == m_epoll.getEventFd())
    {
        m_epoll.drainWakeup();
        return;
    }

    if (!m_socket || fd != m_socket->fd())
    {
        LOG_WARN("unknown fd event fd={} events=0x{:x}", fd, events);
        return;
    }

    if (!m_conn)
    {
        LOG_WARN("event ignored, connection is nullptr fd={} events=0x{:x}",
                 fd,
                 events);
        return;
    }

    if (isClose)
    {
        LOG_INFO("socket close event fd={} events=0x{:x}", fd, events);
        closeConnection();
        return;
    }

    if (isRecv)
    {
        if (!m_handler->handleRecv(fd, *m_conn, m_epoll))
        {
            LOG_WARN("recv handling failed fd={}", fd);
            closeConnection();
            return;
        }
    }

    if (isSend)
    {
        if (!m_handler->handleSend(fd, *m_conn, m_epoll))
        {
            LOG_WARN("send handling failed fd={}", fd);
            closeConnection();
            return;
        }
    }
}

} // namespace pz::icmpd
