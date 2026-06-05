#include "icmp/IcmpEngine.h"

#include "util/Logger.h"

#include <cerrno>

#include <sys/epoll.h>

namespace nf::icmpd
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
        LOG_ERROR("IcmpEngine: epoll add failed fd={}", m_socket->fd());
        closeConnection();
        return false;
    }

    m_initialized = true;

    LOG_INFO("IcmpEngine initialized fd={}", m_socket->fd());
    return true;
}

bool IcmpEngine::poll(int timeoutMs)
{
    const int n = m_epoll.wait(m_events, timeoutMs);
    if (n < 0)
    {
        if (errno == EINTR)
            return true;

        LOG_WARN("IcmpEngine: epoll wait failed errno={}", errno);
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
        LOG_FATAL("IcmpEngineHandler is nullptr");
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
        LOG_WARN("IcmpEngine: enqueue rejected, frame is empty dst={}", dstIp);
        return false;
    }

    if (!m_socket || !m_conn || m_socket->fd() < 0)
    {
        LOG_WARN("IcmpEngine: enqueue rejected, socket is not opened dst={}", dstIp);
        return false;
    }

    if (!m_conn->write(std::move(frame), std::move(dstIp)))
    {
        LOG_WARN("IcmpEngine: tx queue full or invalid frame");
        return false;
    }

    if (!m_epoll.mod(m_socket->fd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP))
    {
        LOG_ERROR("IcmpEngine: epoll mod add EPOLLOUT failed fd={}", m_socket->fd());
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
        LOG_FATAL("IcmpEngineHandler is nullptr");
        return nullptr;
    }

    return m_handler.get();
}

bool IcmpEngine::initEpoll()
{
    if (!m_epoll.init())
    {
        LOG_ERROR("IcmpEngine: epoll init failed");
        return false;
    }

    return true;
}

bool IcmpEngine::initSocket()
{
    if (m_socket)
        return true;

    m_socket = std::make_unique<nf::socket::IcmpSocket>();
    if (!m_socket)
    {
        LOG_ERROR("IcmpEngine: socket allocation failed");
        return false;
    }

    if (!m_socket->open())
    {
        LOG_ERROR("IcmpEngine: socket open failed");
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
        LOG_ERROR("IcmpEngine: connection init failed, socket is not opened");
        return false;
    }

    m_conn = std::make_unique<IcmpConnection>(m_socket->fd());
    if (!m_conn)
    {
        LOG_ERROR("IcmpEngine: connection allocation failed");
        return false;
    }

    return true;
}

void IcmpEngine::closeConnection()
{
    if (m_socket && m_socket->fd() >= 0)
        m_epoll.del(m_socket->fd());

    m_conn.reset();

    if (m_socket)
        m_socket->close();

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
        LOG_WARN("IcmpEngine: unknown fd event fd={} events=0x{:x}", fd, events);
        return;
    }

    if (!m_conn)
    {
        LOG_WARN("IcmpEngine: event ignored, connection is nullptr fd={} events=0x{:x}",
                 fd,
                 events);
        return;
    }

    if (isClose)
    {
        LOG_INFO("IcmpEngine: socket close event fd={} events=0x{:x}", fd, events);
        closeConnection();
        return;
    }

    if (isRecv)
    {
        if (!m_handler->handleRecv(fd, *m_conn, m_epoll))
        {
            LOG_WARN("IcmpEngine: recv handling failed fd={}", fd);
            closeConnection();
            return;
        }
    }

    if (isSend)
    {
        if (!m_handler->handleSend(fd, *m_conn, m_epoll))
        {
            LOG_WARN("IcmpEngine: send handling failed fd={}", fd);
            closeConnection();
            return;
        }
    }
}

} // namespace nf::icmpd
