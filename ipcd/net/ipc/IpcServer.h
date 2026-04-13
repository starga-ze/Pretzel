#pragma once

#include "config/ConfigTypes.h"
#include "net/ipc/IpcConnection.h"
#include "net/ipc/IpcHandler.h"
#include "net/socket/UnixDomainSocket.h"
#include "io/Epoll.h"

#include <atomic>
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

    bool sendTo(int fd, const std::uint8_t* data, std::size_t len);

private:
    bool initEpoll();
    bool initListenSocket();

    void handleEvent(int fd, std::uint32_t events);
    void closeConnection(int fd);

private:
    static constexpr int MAX_EVENTS = 64;

private:
    nf::config::IpcConfig m_cfg;
    nf::io::Epoll m_epoll;
    IpcHandler m_handler;

    std::unique_ptr<UnixDomainSocket> m_listener;
    std::unordered_map<int, std::unique_ptr<IpcConnection>> m_connections;
    std::vector<epoll_event> m_events;

    std::atomic<bool> m_running {false};
    bool m_initialized {false};
};

} // namespace nf::ipcd
