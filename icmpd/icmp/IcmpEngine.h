#pragma once

#include "icmp/IcmpConnection.h"
#include "icmp/IcmpEngineHandler.h"
#include "icmp/IcmpPacket.h"
#include "io/Epoll.h"
#include "socket/IcmpSocket.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <sys/epoll.h>

namespace pz::icmpd
{

class IcmpEngine final
{
public:
    IcmpEngine();
    ~IcmpEngine();

    IcmpEngine(const IcmpEngine&) = delete;
    IcmpEngine& operator=(const IcmpEngine&) = delete;

    IcmpEngine(IcmpEngine&&) = delete;
    IcmpEngine& operator=(IcmpEngine&&) = delete;

    bool init();
    bool poll(int timeoutMs);

    bool sendPacket(std::unique_ptr<IcmpPacket> packet,
                    std::string dstIp);

    bool enqueueFrame(std::vector<std::uint8_t> frame,
                      std::string dstIp);

    bool isOpened() const;
    int fd() const;

    IcmpEngineHandler* handler();

private:
    bool initEpoll();
    bool initSocket();
    bool initConnection();

    bool reopen();
    void closeConnection();
    void handleEvent(int fd, std::uint32_t events);

private:
    static constexpr int MAX_EVENTS = 64;

    // Backoff between socket re-open attempts after a connection drop, so a
    // persistently failing reopen (e.g. lost CAP_NET_RAW) does not spin every tick.
    static constexpr std::chrono::seconds REOPEN_BACKOFF {1};

    bool m_initialized {false};

    std::chrono::steady_clock::time_point m_nextReopenAt {};

    pz::io::Epoll m_epoll;
    std::vector<epoll_event> m_events;

    std::unique_ptr<pz::socket::IcmpSocket> m_socket;
    std::unique_ptr<IcmpConnection> m_conn;
    std::unique_ptr<IcmpEngineHandler> m_handler;
};

} // namespace pz::icmpd
