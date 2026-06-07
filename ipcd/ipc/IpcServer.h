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

namespace pz::ipcd
{

class IpcServer
{
public:
    explicit IpcServer(const pz::config::IpcConfig& cfg, pz::ipc::IpcDaemon selfId);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool init();
    bool poll(int timeoutMs);

    bool enqueueFrame(int fd, std::vector<std::uint8_t> frame);

    IpcServerHandler* handler();

private:
    static constexpr int MAX_EVENTS = 64;

    bool initEpoll();
    bool initListenSocket();

    void handleEvent(int fd, std::uint32_t events);

    std::unique_ptr<pz::socket::UnixDomainSocket> m_listener;

    pz::config::IpcConfig m_cfg;
    pz::ipc::IpcDaemon m_selfId;

    pz::io::Epoll m_epoll;
    std::vector<epoll_event> m_events;

    std::unique_ptr<IpcServerHandler> m_handler;

    pz::ipc::IpcCodec m_codec;
    std::unordered_map<int, std::unique_ptr<pz::ipc::IpcConnection>> m_connections;

    std::atomic<bool> m_running{false};
    bool m_initialized{false};
};

} // namespace pz::ipcd
