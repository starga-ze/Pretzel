#include "ipc/IpcServerHandler.h"

#include "ipc/IpcServer.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <cerrno>
#include <unistd.h>
#include <vector>

using namespace nf::ipc;

namespace nf::ipcd
{

IpcServerHandler::IpcServerHandler(IpcServer* ipcServer,
                                   const nf::config::IpcConfig& cfg)
    : m_ipcServer(ipcServer),
      m_cfg(cfg)
{
}

void IpcServerHandler::handleAccept(
    nf::socket::UnixDomainSocket& listener,
    std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
    nf::io::Epoll& epoll)
{
    while (true)
    {
        const int fd = listener.accept();
        if (fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }

            LOG_ERROR("IpcServerHandler: accept failed errno={}", errno);
            break;
        }

        if (connections.size() >= static_cast<std::size_t>(m_cfg.maxConnections))
        {
            LOG_WARN("IpcServerHandler: max connections reached max={} fd={}",
                     m_cfg.maxConnections,
                     fd);
            ::close(fd);
            continue;
        }

        auto conn = std::make_unique<IpcConnection>(
            fd,
            static_cast<std::size_t>(m_cfg.rxBufferSize),
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

        LOG_INFO("IpcServerHandler: accepted connection fd={} total={}",
                 fd,
                 connections.size());
    }
}

bool IpcServerHandler::handleRecv(
    int fd,
    std::unordered_map<int, std::unique_ptr<IpcConnection>>& connections,
    nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
    {
        return false;
    }

    if (!IpcHandler::handleRecv(fd, *it->second, epoll))
    {
        closeConnection(fd, connections, epoll);
        return false;
    }

    return true;
}

bool IpcServerHandler::handleSend(
    int fd,
    std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>>& connections,
    nf::io::Epoll& epoll)
{
    auto it = connections.find(fd);
    if (it == connections.end())
    {
        return false;
    }

    if (!nf::ipc::IpcHandler::handleSend(fd, *it->second, epoll))
    {
        closeConnection(fd, connections, epoll);
        return false;
    }

    return true;
}

void IpcServerHandler::closeConnection(
    int fd,
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

bool IpcServerHandler::ingress(int fd, nf::ipc::IpcFrameView frame)
{
    if (frame.empty())
    {
        LOG_WARN("Ingress frame is empty: fd={}", fd);
        return false;
    }

    if (!m_rxRouter)
    {
        LOG_WARN("RxRouter is nullptr");
        return false;
    }

    std::unique_ptr<nf::ipc::IpcMessage> msg;

    const auto rc = m_codec.decode(frame, msg);
    if (rc != nf::ipc::IpcDecodeResult::Ok)
    {
        LOG_ERROR("Ingress decode failed: fd={} rc={} frameSize={}",
                  fd,
                  static_cast<int>(rc),
                  frame.size);
        return false;
    }

    if (!msg)
    {
        LOG_ERROR("Ingress decode returned null message: fd={} frameSize={}",
                  fd,
                  frame.size);
        return false;
    }

    if (!bindRoute(msg->getSrc(), fd))
    {
        LOG_ERROR("Ingress rejected: route validation failed, src={}, fd={}",
                nf::ipc::IpcProtocol::daemonToStr(msg->getSrc()),
                fd);
        return false;
    }

    LOG_TRACE("IPC Ingress Message dump:\n{}", msg->dump());

    m_rxRouter->handleIpcMessage(std::move(msg));

    return true;
}

void IpcServerHandler::egress(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("Egress message is nullptr");
        return;
    }

    if (!m_ipcServer)
    {
        LOG_ERROR("ipcServer is nullptr");
        return;
    }

    if (msg->getDst() == nf::ipc::IpcDaemon::Broadcast ||
        nf::ipc::IpcProtocol::hasFlag(msg->getFlags(), nf::ipc::IpcFlag::Broadcast))
    {
        broadcast(std::move(msg));
        return;
    }

    unicast(std::move(msg));
}

void IpcServerHandler::unicast(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        return;
    }

    const int fd = findRoute(msg->getDst());
    if (fd < 0)
    {
        LOG_WARN("No route for unicast dst={}",
                 nf::ipc::IpcProtocol::daemonToStr(msg->getDst()));
        return;
    }

    sendFrame(fd, msg);
}

void IpcServerHandler::broadcast(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        return;
    }

    LOG_TRACE("IPC Broadcast Message dump:\n{}", msg->dump());

    std::vector<std::uint8_t> frame = m_codec.encode(msg);
    if (frame.empty())
    {
        LOG_WARN("Broadcast encode failed: cmd={} payload={}bytes",
                 nf::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
                 msg->getPayloadLen());
        return;
    }

    for (const auto& [daemon, state] : m_runtimeTable)
    {
        if (state.fd < 0)
        {
            continue;
        }

        if (daemon == msg->getSrc() ||
            daemon == nf::ipc::IpcDaemon::Ipcd ||
            daemon == nf::ipc::IpcDaemon::Engined ||
            daemon == nf::ipc::IpcDaemon::Broadcast)
        {
            continue;
        }

        std::vector<std::uint8_t> copiedFrame = frame;

        if (!m_ipcServer->enqueueFrame(state.fd, std::move(copiedFrame)))
        {
            LOG_WARN("Broadcast enqueue failed: daemon={} fd={} frame={}bytes",
                     nf::ipc::IpcProtocol::daemonToStr(daemon),
                     state.fd,
                     frame.size());
        }
    }
}

bool IpcServerHandler::sendFrame(
    int fd,
    const std::unique_ptr<nf::ipc::IpcMessage>& msg)
{
    if (!msg)
    {
        LOG_WARN("sendFrame message is nullptr");
        return false;
    }

    LOG_TRACE("IPC Egress Message dump:\n{}", msg->dump());

    std::vector<std::uint8_t> frame = m_codec.encode(msg);
    if (frame.empty())
    {
        LOG_WARN("Egress encode failed: fd={} dst={} cmd={} payload={}bytes",
                 fd,
                 nf::ipc::IpcProtocol::daemonToStr(msg->getDst()),
                 nf::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
                 msg->getPayloadLen());
        return false;
    }

    const std::size_t frameSize = frame.size();

    if (!m_ipcServer->enqueueFrame(fd, std::move(frame)))
    {
        LOG_WARN("Egress enqueue failed: fd={} frame={}bytes", fd, frameSize);
        return false;
    }

    return true;
}

bool IpcServerHandler::bindRoute(nf::ipc::IpcDaemon daemon, int fd)
{
    auto it = m_runtimeTable.find(daemon);
    if (it == m_runtimeTable.end())
    {
        RuntimeState state;
        state.fd = fd;
        state.ready = false;

        m_runtimeTable.emplace(daemon, state);

        LOG_DEBUG("Runtime route added: daemon={}, fd={}, entry={}",
                  nf::ipc::IpcProtocol::daemonToStr(daemon),
                  fd,
                  m_runtimeTable.size());
        return true;
    }

    RuntimeState& state = it->second;

    if (state.fd == fd)
    {
        LOG_TRACE("Runtime route unchanged: daemon={}, fd={}, ready={}",
                  nf::ipc::IpcProtocol::daemonToStr(daemon),
                  fd,
                  state.ready);
        return true;
    }

    if (state.fd >= 0 && state.fd != fd)
    {
        LOG_ERROR("Runtime route hijack attempt blocked: daemon={} oldFd={} newFd={}",
                  nf::ipc::IpcProtocol::daemonToStr(daemon),
                  state.fd,
                  fd);
        return false;
    }

    state.fd = fd;
    state.ready = false;

    LOG_DEBUG("Runtime route rebound: daemon={}, fd={}, ready=false",
              nf::ipc::IpcProtocol::daemonToStr(daemon),
              fd);

    return true;
}

int IpcServerHandler::findRoute(nf::ipc::IpcDaemon daemon) const
{
    auto it = m_runtimeTable.find(daemon);
    if (it == m_runtimeTable.end())
    {
        return -1;
    }

    const RuntimeState& state = it->second;

    if (state.fd < 0)
    {
        return -1;
    }

    return state.fd;
}

void IpcServerHandler::removeRoute(int fd)
{
    for (auto& [daemon, state] : m_runtimeTable)
    {
        if (state.fd != fd)
        {
            continue;
        }

        state.fd = -1;
        state.ready = false;

        LOG_DEBUG("Runtime route removed: daemon={}, fd={}",
                  nf::ipc::IpcProtocol::daemonToStr(daemon),
                  fd);
    }
}

void IpcServerHandler::markRuntimeReady(nf::ipc::IpcDaemon daemon, bool ready)
{
    auto it = m_runtimeTable.find(daemon);
    if (it == m_runtimeTable.end())
    {
        LOG_WARN("markRuntimeReady failed: daemon={} not found",
                 nf::ipc::IpcProtocol::daemonToStr(daemon));
        return;
    }

    it->second.ready = ready;

    LOG_INFO("Runtime ready changed: daemon={}, fd={}, ready={}",
             nf::ipc::IpcProtocol::daemonToStr(daemon),
             it->second.fd,
             ready);
}

const std::unordered_map<nf::ipc::IpcDaemon, RuntimeState>& IpcServerHandler::getRuntimeTable() const
{
    return m_runtimeTable;
}

void IpcServerHandler::setRxRouter(nf::router::RxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
}

} // namespace nf::ipcd
