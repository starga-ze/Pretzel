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

namespace nf::ipc
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
    IpcClient(const nf::config::IpcConfig& cfg, IpcDaemon selfId);
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    IpcClient(IpcClient&&) = delete;
    IpcClient& operator=(IpcClient&&) = delete;

    bool init();
    void start();
    void stop();

    bool send(const IpcMessage& msg);

    bool send(IpcDaemon dst,
              IpcCmd cmd,
              const std::uint8_t* payload,
              std::size_t len,
              std::uint8_t flags = static_cast<std::uint8_t>(IpcFlag::Request));

    bool send(IpcDaemon dst,
              IpcCmd cmd,
              const std::vector<std::uint8_t>& payload,
              std::uint8_t flags = static_cast<std::uint8_t>(IpcFlag::Request));

    State state() const;
    bool isConnected() const;
    int fd() const;

private:
    bool initEpoll();
    bool initSocket();
    bool connectServer();

    void closeConnection();

    void handleEvent(int fd, std::uint32_t events);
    void handleConnectEvent();

    std::uint32_t nextSeqNo();

private:
    static constexpr int MAX_EVENTS = 32;

private:
    nf::config::IpcConfig m_cfg;
    IpcDaemon m_selfId;

    nf::io::Epoll m_epoll;
    std::vector<epoll_event> m_events;

    std::unique_ptr<nf::socket::UnixDomainSocket> m_socket;
    std::unique_ptr<IpcConnection> m_conn;

    IpcCodec m_codec;
    IpcClientHandler m_handler;

    std::atomic<bool> m_running {false};
    bool m_initialized {false};
    State m_state {State::Disconnected};

    std::uint32_t m_seqNo {1};
};

} // namespace nf::ipc
