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
      m_events(MAX_EVENTS)
{
}

IpcClient::~IpcClient()
{
    stop();
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

    LOG_INFO("IpcClient initialized path={}", m_cfg.socketPath);
    return true;
}

void IpcClient::start()
{
    if (!init())
    {
        LOG_ERROR("IpcClient: init failed");
        return;
    }

    m_running = true;

    LOG_INFO("IpcClient start");

    while (m_running)
    {
        const int n = m_epoll.wait(m_events, -1);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            LOG_WARN("IpcClient: epoll wait failed errno={}", errno);
            continue;
        }

        for (int i = 0; i < n; ++i)
        {
            const int fd = m_events[i].data.fd;
            const std::uint32_t events = m_events[i].events;

            handleEvent(fd, events);
        }
    }

    LOG_INFO("IpcClient stopped");
}

void IpcClient::stop()
{
    m_running = false;
    m_epoll.wakeup();
}

bool IpcClient::send(IpcDaemon dst, IpcCmd cmd, const std::uint8_t* payload, std::size_t len)
{
    if (!m_conn || !m_socket || m_state != State::Connected)
    {
        LOG_WARN("IpcClient: send rejected, not connected");
        return false;
    }

    auto frame = buildFrame(m_selfId, dst, cmd, payload, len);

    {
        std::lock_guard<std::mutex> lock(m_txMutex);

        auto& tx = m_conn->tx();
        if (tx.writable() < frame.size())
        {
            LOG_WARN("IpcClient: tx buffer full writable={} need={}",
                     tx.writable(),
                     frame.size());
            return false;
        }

        tx.write(frame.data(), frame.size());
    }

    if (!m_epoll.mod(m_socket->fd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP))
    {
        LOG_ERROR("IpcClient: epoll mod add EPOLLOUT failed fd={}", m_socket->fd());
        closeConnection();
        return false;
    }

    m_epoll.wakeup();
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
    if (fd == m_epoll.getEventFd())
    {
        m_epoll.drainWakeup();
        return;
    }

    if (!m_socket || fd != m_socket->fd())
    {
        LOG_WARN("IpcClient: unknown fd event fd={} events=0x{:x}", fd, events);
        return;
    }

    const bool isClose = (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
    const bool isRecv  = (events & EPOLLIN) != 0;
    const bool isSend  = (events & EPOLLOUT) != 0;

    if (isClose)
    {
        LOG_INFO("IpcClient: close event fd={} events=0x{:x}", fd, events);
        closeConnection();
        return;
    }

    if (m_state == State::Connecting)
    {
        if (isRecv || isSend)
            handleConnectEvent();

        return;
    }

    if (m_state != State::Connected)
        return;

    if (isRecv)
        handleRecv();

    if (isSend)
        handleSend();
}

void IpcClient::handleConnectEvent()
{
    if (!m_socket)
        return;

    int soError = 0;
    socklen_t len = sizeof(soError);

    if (::getsockopt(m_socket->fd(), SOL_SOCKET, SO_ERROR, &soError, &len) != 0)
    {
        LOG_ERROR("IpcClient: getsockopt(SO_ERROR) failed fd={} errno={}",
                  m_socket->fd(),
                  errno);
        closeConnection();
        return;
    }

    if (soError != 0)
    {
        LOG_ERROR("IpcClient: connect completion failed fd={} so_error={}",
                  m_socket->fd(),
                  soError);
        closeConnection();
        return;
    }

    m_state = State::Connected;

    if (!m_epoll.mod(m_socket->fd(), EPOLLIN | EPOLLRDHUP))
    {
        LOG_ERROR("IpcClient: epoll mod after connect failed fd={}", m_socket->fd());
        closeConnection();
        return;
    }

    LOG_INFO("IpcClient: connected fd={}", m_socket->fd());
}

void IpcClient::handleRecv()
{
    if (!m_conn || !m_socket)
        return;

    int ioErrno = 0;
    const IoResult rc = m_conn->recv(ioErrno);

    switch (rc)
    {
    case IoResult::Ok:
    case IoResult::WouldBlock:
        break;

    case IoResult::PeerClosed:
        LOG_INFO("IpcClient: peer closed fd={}", m_socket->fd());
        closeConnection();
        return;

    case IoResult::BufferFull:
        LOG_WARN("IpcClient: rx buffer full fd={}, closing", m_socket->fd());
        closeConnection();
        return;

    case IoResult::Error:
        LOG_ERROR("IpcClient: recv failed fd={} errno={}", m_socket->fd(), ioErrno);
        closeConnection();
        return;
    }

    auto& rx = m_conn->rx();
    if (rx.readable() > 0)
    {
        LOG_DEBUG("IpcClient: received {} bytes from server", rx.readable());

        // TODO:
        // frame parser 붙이면 여기서 처리
        // 현재는 transport layer 검증 용도로 모두 consume
        rx.consume(rx.readable());
    }
}

void IpcClient::handleSend()
{
    if (!m_conn || !m_socket)
        return;

    std::lock_guard<std::mutex> lock(m_txMutex);

    int ioErrno = 0;
    const IoResult rc = m_conn->send(ioErrno);

    switch (rc)
    {
    case IoResult::Ok:
        if (!m_epoll.mod(m_socket->fd(), EPOLLIN | EPOLLRDHUP))
        {
            LOG_ERROR("IpcClient: epoll mod remove EPOLLOUT failed fd={}", m_socket->fd());
            closeConnection();
        }
        return;

    case IoResult::WouldBlock:
        if (!m_epoll.mod(m_socket->fd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP))
        {
            LOG_ERROR("IpcClient: epoll mod keep EPOLLOUT failed fd={}", m_socket->fd());
            closeConnection();
        }
        return;

    case IoResult::PeerClosed:
        LOG_INFO("IpcClient: peer closed while flushing fd={}", m_socket->fd());
        closeConnection();
        return;

    case IoResult::BufferFull:
        LOG_ERROR("IpcClient: unexpected BufferFull during flush fd={}", m_socket->fd());
        closeConnection();
        return;

    case IoResult::Error:
        LOG_ERROR("IpcClient: flushTx failed fd={} errno={}", m_socket->fd(), ioErrno);
        closeConnection();
        return;
    }
}

} // namespace nf::ipc
