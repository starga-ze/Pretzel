#pragma once

#include "config/ConfigTypes.h"
#include "net/ipc/IpcConnection.h"
#include "net/socket/UnixDomainSocket.h"
#include "io/Epoll.h"

#include <atomic>
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

private:
    bool initEpoll();
    bool initListener();
    bool registerListenFd();

    void handleEvent(int fd, uint32_t events);
    void handleListenEvent(uint32_t events);
    void acceptLoop();

    void closeConnection(int fd);

private:
    static constexpr int MAX_EVENTS = 64;

private:
    nf::config::IpcConfig m_cfg;
    nf::io::Epoll m_epoll;

    std::unique_ptr<UnixDomainSocket> m_listener;
    std::unordered_map<int, std::unique_ptr<IpcConnection>> m_connections;
    std::vector<epoll_event> m_events;

    std::atomic<bool> m_running {false};
    bool m_initialized {false};
};

} // namespace nf::ipcd
