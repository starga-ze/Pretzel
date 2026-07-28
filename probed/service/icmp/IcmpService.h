#pragma once

#include "event/ProbedEvent.h"
#include "icmp/IcmpPacket.h"
#include "service/icmp/IcmpAction.h"
#include "service/icmp/IcmpEvent.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pz::probed
{

class ProbedEventFactory;
class ProbedActionFactory;
class ProbedServiceManager;

class IcmpService final
{
public:
    IcmpService(ProbedEventFactory* eventFactory, ProbedActionFactory* actionFactory);

    void start();

    std::unique_ptr<ProbedEvent> schedule(std::chrono::steady_clock::time_point now);

    void handleEvent(ProbedServiceManager& serviceManager, const IcmpEvent& event);
    void handleAction(ProbedServiceManager& serviceManager, const IcmpAction& action);

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
    void sendProbeBatch(ProbedServiceManager& serviceManager);
    void onEchoReply(const IcmpEvent& event);
    void completeProbeSession();
    void sendProbeResult(ProbedServiceManager& serviceManager);

    bool allProbeSent() const;
    bool replyWaitExpired(std::chrono::steady_clock::time_point now) const;
    bool canAcceptReply() const;

    std::unique_ptr<IcmpPacket> buildEchoRequestPacket(std::uint16_t sequence) const;

    static std::uint32_t ipv4ToHostU32(const std::string& ip);

private:
    ProbedEventFactory* m_eventFactory = nullptr;
    ProbedActionFactory* m_actionFactory = nullptr;

    State m_state = State::Init;

    std::vector<ProbeTarget> m_targets;
    std::unordered_map<std::string, std::size_t> m_targetIndexByIp;
    std::unordered_set<std::string> m_localIps;

    std::size_t m_nextSendIndex = 0;

    std::chrono::steady_clock::time_point m_probeStartedAt{};
    std::chrono::steady_clock::time_point m_lastBatchSentAt{};
    std::chrono::steady_clock::time_point m_waitingStartedAt{};
    std::chrono::steady_clock::time_point m_lastProbeCompletedAt{};
    std::chrono::steady_clock::time_point m_lastReplyAt{};

    std::uint16_t m_identifier = 0;
    std::uint32_t m_lastAliveCount = 0;
};

}
