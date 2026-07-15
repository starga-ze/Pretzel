#pragma once

#include "config/ConfigTypes.h"
#include "io/Epoll.h"
#include "socket/UnixDomainSocket.h"

#include "ipc/IpcClientHandler.h"
#include "ipc/IpcCodec.h"
#include "ipc/IpcConnection.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <sys/epoll.h>

namespace pz::ipc
{

class IpcClient
{
public:
    enum class State
    {
        Disconnected,
        Connecting,
        Connected
    };

public:
    IpcClient(const pz::config::IpcConfig& cfg, IpcDaemon selfId);
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    IpcClient(IpcClient&&) = delete;
    IpcClient& operator=(IpcClient&&) = delete;

    bool init();
    bool poll(int timeoutMs);
    bool enqueueFrame(std::vector<std::uint8_t> frame);

    State state() const;
    bool isConnected() const;
    int fd() const;
    IpcClientHandler* handler();

private:
    bool initEpoll();
    bool initSocket();
    bool connectServer();

    void closeConnection();

    void handleEvent(int fd, std::uint32_t events);
    void handleConnectEvent();

private:
    static constexpr int MAX_EVENTS = 32;

private:
    pz::config::IpcConfig m_cfg;
    IpcDaemon m_selfId;

    pz::io::Epoll m_epoll;
    std::vector<epoll_event> m_events;

    std::unique_ptr<pz::socket::UnixDomainSocket> m_socket;
    std::unique_ptr<IpcConnection> m_conn;

    IpcCodec m_codec;
    std::unique_ptr<IpcClientHandler> m_handler;

    std::atomic<bool> m_running{false};
    bool m_initialized{false};
    State m_state{State::Disconnected};
};

}
