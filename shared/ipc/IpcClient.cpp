#include "ipc/IpcClient.h"

#include "util/Logger.h"

#include <cerrno>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/socket.h>

namespace nf::ipc
{

IpcClient::IpcClient(const nf::config::IpcConfig& cfg, IpcDaemon selfId)
    : m_cfg(cfg),
      m_selfId(selfId),
      m_events(MAX_EVENTS),
      m_handler(std::make_unique<IpcClientHandler>(this))
{
}

IpcClient::~IpcClient()
{
    closeConnection();
}

bool IpcClient::init()
{
    if (m_initialized)
        return true;

    if (!initEpoll())
        return false;

    if (!initSocket())
        return false;

    if (!connectServer())
        return false;

    m_initialized = true;

    LOG_INFO("IpcClient initialized path={} self={}",
             m_cfg.socketPath,
             IpcProtocol::daemonToStr(m_selfId));
    return true;
}

bool IpcClient::poll(int timeoutMs)
{
    const int n = m_epoll.wait(m_events, timeoutMs);
    if (n < 0)
    {
        if (errno == EINTR)
        {
            return true;
        }

        LOG_WARN("Epoll wait failed errno={}", errno);
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

bool IpcClient::send(std::unique_ptr<IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("IpcClient: send rejected, message is nullptr");
        return false;
    }

    if (!m_conn || !m_socket || m_state != State::Connected)
    {
        LOG_WARN("IpcClient: send rejected, not connected");
        return false;
    }

    LOG_DEBUG("Tx Ipc Message dump:\n{}", msg->dump());

    std::vector<std::uint8_t> frame = m_codec.encode(msg);
    if (frame.empty())
    {
        LOG_WARN("IpcClient: encode failed cmd={} payload={}bytes",
                 IpcProtocol::cmdToStr(msg->getCmd()),
                 msg->getPayloadLen());
        return false;
    }

    if (!m_conn->write(frame))
    {
        LOG_WARN("IpcClient: tx buffer full frame={}bytes", frame.size());
        return false;
    }

    if (!m_epoll.mod(m_socket->fd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP))
    {
        LOG_ERROR("IpcClient: epoll mod add EPOLLOUT failed fd={}", m_socket->fd());
        closeConnection();
        return false;
    }

    return true;
}

IpcClient::State IpcClient::state() const
{
    return m_state;
}

bool IpcClient::isConnected() const
{
    return m_state == State::Connected;
}

int IpcClient::fd() const
{
    return m_socket ? m_socket->fd() : -1;
}

bool IpcClient::initEpoll()
{
    if (!m_epoll.init())
    {
        LOG_ERROR("IpcClient: epoll init failed");
        return false;
    }

    return true;
}

bool IpcClient::initSocket()
{
    if (m_socket)
        return true;

    m_socket = std::make_unique<nf::socket::UnixDomainSocket>(m_cfg.socketPath);
    if (!m_socket)
    {
        LOG_ERROR("IpcClient: socket allocation failed");
        return false;
    }

    return true;
}

bool IpcClient::connectServer()
{
    const auto rc = m_socket->connect();
    if (rc == nf::socket::UnixDomainSocket::ConnectResult::Failed)
    {
        LOG_ERROR("IpcClient: connect failed path={}", m_cfg.socketPath);
        return false;
    }

    m_conn = std::make_unique<IpcConnection>(
        m_socket->fd(),
        static_cast<std::size_t>(m_cfg.rxBufferSize),
        static_cast<std::size_t>(m_cfg.txBufferSize));

    if (!m_conn)
    {
        LOG_ERROR("IpcClient: connection allocation failed");
        closeConnection();
        return false;
    }

    if (rc == nf::socket::UnixDomainSocket::ConnectResult::Connected)
    {
        m_state = State::Connected;

        if (!m_epoll.add(m_socket->fd(), EPOLLIN | EPOLLRDHUP))
        {
            LOG_ERROR("IpcClient: epoll add connected fd failed fd={}", m_socket->fd());
            closeConnection();
            return false;
        }

        LOG_INFO("IpcClient: connected immediately fd={}", m_socket->fd());
        return true;
    }

    m_state = State::Connecting;

    if (!m_epoll.add(m_socket->fd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP))
    {
        LOG_ERROR("IpcClient: epoll add connecting fd failed fd={}", m_socket->fd());
        closeConnection();
        return false;
    }

    LOG_INFO("IpcClient: connecting fd={}", m_socket->fd());
    return true;
}

void IpcClient::closeConnection()
{
    if (m_socket && m_socket->fd() >= 0)
        m_epoll.del(m_socket->fd());

    m_conn.reset();

    if (m_socket)
        m_socket->close();

    m_state = State::Disconnected;
}

void IpcClient::handleEvent(int fd, std::uint32_t events)
{
    const bool isClose = (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
    const bool isRecv  = (events & EPOLLIN) != 0;
    const bool isSend  = (events & EPOLLOUT) != 0;

    /* Event fd */
    if (fd == m_epoll.getEventFd())
    {
        m_epoll.drainWakeup();
        return;
    }

    /* Socket fd */
    if (!m_socket || fd != m_socket->fd())
    {
        LOG_WARN("IpcClient: unknown fd event fd={} events=0x{:x}", fd, events);
        return;
    }

    /* Connecting fd */
    if (m_state == State::Connecting)
    {
        if (isClose || isRecv || isSend)
        {
            if (!m_handler->handleConnect(fd, m_epoll))
            {
                closeConnection();
                return;
            }

            m_state = State::Connected;
        }

        return;
    }

    /* Connected fd */
    if (m_state != State::Connected || !m_conn)
    {
        LOG_WARN("IpcClient: event ignored in invalid state fd={} state={} events=0x{:x}",
                 fd,
                 static_cast<int>(m_state),
                 events);
        return;
    }

    if (isClose)
    {
        LOG_INFO("IpcClient: connection close event fd={} events=0x{:x}", fd, events);
        closeConnection();
        return;
    }

    if (isRecv)
    {
        if (!m_handler->handleRecv(fd, *m_conn, m_epoll))
        {
            closeConnection();
            return;
        }
    }

    if (isSend)
    {
        if (!m_handler->handleSend(fd, *m_conn, m_epoll))
        {
            closeConnection();
            return;
        }
    }
}

IpcClientHandler* IpcClient::handler()
{
    if(!m_handler)
    {
        LOG_FATAL("IpcClientHandler is nullptr");
        return nullptr;
    }
    return m_handler.get();
}

} // namespace nf::ipc
