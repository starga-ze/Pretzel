// service/probe/ProbeService.h
#pragma once

#include "event/IcmpdEvent.h"
#include "icmp/IcmpPacket.h"
#include "service/probe/ProbeAction.h"
#include "service/probe/ProbeEvent.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pz::icmpd
{

class IcmpdEventFactory;
class IcmpdActionFactory;
class IcmpdServiceManager;

class ProbeService final
{
public:
    ProbeService(IcmpdEventFactory* eventFactory,
                 IcmpdActionFactory* actionFactory);

    void start();

    std::unique_ptr<IcmpdEvent> schedule(std::chrono::steady_clock::time_point now);

    void handleEvent(IcmpdServiceManager& serviceManager, const ProbeEvent& event);
    void handleAction(IcmpdServiceManager& serviceManager, const ProbeAction& action);

private:
    enum class State
    {
        Init = 0,
        Idle = 1,
        Sending = 2,
        WaitingReplies = 3,
    };

    struct ProbeTarget
    {
        std::string ip;
        std::uint16_t sequence = 0;
        bool alive = false;
    };

private:
    void beginProbeSession();
    void sendProbeBatch(IcmpdServiceManager& serviceManager);
    void onEchoReply(const ProbeEvent& event);
    void completeProbeSession();
    void sendProbeResult(IcmpdServiceManager& serviceManager);

    bool allProbeSent() const;
    bool replyWaitExpired(std::chrono::steady_clock::time_point now) const;
    bool canAcceptReply() const;

    std::unique_ptr<IcmpPacket> buildEchoRequestPacket(std::uint16_t sequence) const;

    static std::vector<ProbeTarget> buildIpv4Targets(const std::string& cidr);
    static std::uint32_t ipv4ToHostU32(const std::string& ip);
    static std::string hostU32ToIpv4(std::uint32_t value);

private:
    IcmpdEventFactory* m_eventFactory = nullptr;
    IcmpdActionFactory* m_actionFactory = nullptr;

    State m_state = State::Init;

    std::vector<ProbeTarget> m_targets;
    std::unordered_map<std::string, std::size_t> m_targetIndexByIp;
    std::unordered_set<std::string> m_localIps;

    std::size_t m_nextSendIndex = 0;

    std::chrono::steady_clock::time_point m_probeStartedAt {};
    std::chrono::steady_clock::time_point m_lastBatchSentAt {};
    std::chrono::steady_clock::time_point m_waitingStartedAt {};
    std::chrono::steady_clock::time_point m_lastProbeCompletedAt {};
    std::chrono::steady_clock::time_point m_lastReplyAt {};

    std::uint16_t m_identifier = 0;
    std::uint32_t m_lastAliveCount = 0;  // 마지막 probe 사이클의 alive device 수

    static constexpr std::size_t kProbeBatchSize = 32;
    static constexpr auto kProbeBatchInterval = std::chrono::milliseconds(20);

    static constexpr auto kProbeCycleInterval = std::chrono::seconds(30);
    static constexpr auto kReplyIdleTimeout = std::chrono::seconds(5);
    static constexpr auto kReplyMaxWaitTimeout = std::chrono::seconds(15);
};

} // namespace pz::icmpd
