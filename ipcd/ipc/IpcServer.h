#pragma once

#include "config/ConfigTypes.h"
#include "io/Epoll.h"
#include "socket/UnixDomainSocket.h"

#include "ipc/IpcServerHandler.h"
#include "ipc/IpcCodec.h"
#include "ipc/IpcConnection.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>

namespace nf::ipcd
{

class IpcServer
{
public:
    explicit IpcServer(const nf::config::IpcConfig& cfg, nf::ipc::IpcDaemon selfId);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool init();
    void start();
    void stop();

    bool enqueueMessage(int fd, std::unique_ptr<nf::ipc::IpcMessage> msg);

private:
    bool initEpoll();
    bool initListenSocket();

    void handleEvent(int fd, std::uint32_t events);

private:
    static constexpr int MAX_EVENTS = 64;

private:
    nf::config::IpcConfig m_cfg;
    nf::ipc::IpcDaemon m_selfId;

    nf::io::Epoll m_epoll;
    std::vector<epoll_event> m_events;

    nf::ipc::IpcCodec m_codec;
    IpcServerHandler m_handler;

    std::unique_ptr<nf::socket::UnixDomainSocket> m_listener;
    std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>> m_connections;

    std::atomic<bool> m_running{false};
    bool m_initialized{false};
};

} // namespace nf::ipcd
