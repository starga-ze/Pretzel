#pragma once

#include "config/ConfigTypes.h"
#include "ipc/IpcHandler.h"
#include "socket/UnixDomainSocket.h"
#include "io/Epoll.h"
#include "ipc/IpcConnection.h"

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
    explicit IpcServer(const nf::config::IpcConfig& cfg);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool init();
    void start();
    void stop();

    bool send(int fd, const std::uint8_t* data, std::size_t len);

private:
    bool initEpoll();
    bool initListenSocket();

    void handleEvent(int fd, std::uint32_t events);

private:
    static constexpr int MAX_EVENTS = 64;

private:
    nf::config::IpcConfig m_cfg;
    nf::io::Epoll m_epoll;
    IpcHandler m_handler;

    std::unique_ptr<nf::socket::UnixDomainSocket> m_listener;
    std::unordered_map<int, std::unique_ptr<nf::ipc::IpcConnection>> m_connections;
    std::vector<epoll_event> m_events;

    std::atomic<bool> m_running{false};
    bool m_initialized{false};
};

} // namespace nf::ipcd
