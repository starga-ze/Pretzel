// service/probe/ProbeService.cpp
#include "service/probe/ProbeService.h"

#include "action/IcmpdActionFactory.h"
#include "event/IcmpdEventFactory.h"
#include "router/IcmpdTxRouter.h"
#include "service/IcmpdServiceManager.h"
#include "util/Logger.h"

#include <arpa/inet.h>
#include <unistd.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace nf::icmpd
{

ProbeService::ProbeService(IcmpdEventFactory* eventFactory,
                           IcmpdActionFactory* actionFactory)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory)
{
}

void ProbeService::start()
{
    m_state = State::Init;

    m_localIps.clear();
    m_localIps.insert("192.168.0.82");

    LOG_INFO("ProbeService start");
}

std::unique_ptr<IcmpdEvent> ProbeService::schedule(std::chrono::steady_clock::time_point now)
{
    if (!m_eventFactory)
    {
        LOG_ERROR("ProbeService: EventFactory is nullptr");
        return nullptr;
    }

    switch (m_state)
    {
    case State::Init:
    {
        return m_eventFactory->create(
            IcmpdEventDomain::Probe,
            static_cast<std::uint32_t>(ProbeEventType::StartProbe));
    }

    case State::Idle:
    {
        if (now - m_lastProbeCompletedAt >= kProbeCycleInterval)
        {
            return m_eventFactory->create(
                IcmpdEventDomain::Probe,
                static_cast<std::uint32_t>(ProbeEventType::StartProbe));
        }

        return nullptr;
    }

    case State::Sending:
    {
        if (!allProbeSent() && now - m_lastBatchSentAt >= kProbeBatchInterval)
        {
            return m_eventFactory->create(
                IcmpdEventDomain::Probe,
                static_cast<std::uint32_t>(ProbeEventType::SendProbeBatch));
        }

        if (allProbeSent())
        {
            m_waitingStartedAt = now;
            m_lastReplyAt = now;
            m_state = State::WaitingReplies;

            LOG_INFO("All probes sent, waiting replies (idle_timeout={}ms, max_timeout={}ms)",
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         kReplyIdleTimeout).count(),
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         kReplyMaxWaitTimeout).count());
        }

        return nullptr;
    }

    case State::WaitingReplies:
    {
        if (replyWaitExpired(now))
        {
            return m_eventFactory->create(
                IcmpdEventDomain::Probe,
                static_cast<std::uint32_t>(ProbeEventType::ProbeCompleted));
        }

        return nullptr;
    }
    }

    return nullptr;
}

void ProbeService::handleEvent(IcmpdServiceManager& serviceManager, const ProbeEvent& event)
{
    if (!m_actionFactory)
    {
        LOG_ERROR("ProbeService: ActionFactory is nullptr");
        return;
    }

    switch (event.type())
    {
    case ProbeEventType::StartProbe:
    {
        auto action = m_actionFactory->create(
            IcmpdActionDomain::Probe,
            static_cast<std::uint32_t>(ProbeActionType::StartProbe));

        serviceManager.postAction(std::move(action));
        break;
    }

    case ProbeEventType::SendProbeBatch:
    {
        auto action = m_actionFactory->create(
            IcmpdActionDomain::Probe,
            static_cast<std::uint32_t>(ProbeActionType::SendProbeBatch));

        serviceManager.postAction(std::move(action));
        break;
    }

    case ProbeEventType::EchoReply:
    {
        onEchoReply(event);
        break;
    }

    case ProbeEventType::ProbeCompleted:
    {
        completeProbeSession();
        break;
    }

    default:
    {
        LOG_DEBUG("ProbeService: unhandled event type={}",
                  static_cast<std::uint32_t>(event.type()));
        break;
    }
    }
}

void ProbeService::handleAction(IcmpdServiceManager& serviceManager,
                                const ProbeAction& action)
{
    switch (action.type())
    {
    case ProbeActionType::StartProbe:
    {
        if (m_state == State::Sending || m_state == State::WaitingReplies)
        {
            LOG_DEBUG("ProbeService: skip StartProbe while active state={}",
                      static_cast<int>(m_state));
            return;
        }

        beginProbeSession();
        break;
    }

    case ProbeActionType::SendProbeBatch:
    {
        if (m_state != State::Sending)
        {
            LOG_DEBUG("ProbeService: skip SendProbeBatch state={}",
                      static_cast<int>(m_state));
            return;
        }

        sendProbeBatch(serviceManager);
        break;
    }

    default:
    {
        LOG_DEBUG("ProbeService: unhandled action type={}",
                  static_cast<std::uint32_t>(action.type()));
        break;
    }
    }
}

void ProbeService::beginProbeSession()
{
    m_identifier = static_cast<std::uint16_t>(::getpid() & 0xffff);

    m_targets = buildIpv4Targets("192.168.0.0/23");
    m_targetIndexByIp.clear();

    const std::size_t generatedCount = m_targets.size();

    m_targets.erase(
        std::remove_if(
            m_targets.begin(),
            m_targets.end(),
            [this](const ProbeTarget& target) {
                return m_localIps.find(target.ip) != m_localIps.end();
            }),
        m_targets.end());

    for (std::size_t i = 0; i < m_targets.size(); ++i)
    {
        m_targets[i].sequence = static_cast<std::uint16_t>(i + 1);
        m_targets[i].alive = false;
        m_targetIndexByIp.emplace(m_targets[i].ip, i);
    }

    m_nextSendIndex = 0;

    const auto now = std::chrono::steady_clock::now();
    m_probeStartedAt = now;
    m_lastBatchSentAt = now - kProbeBatchInterval;
    m_waitingStartedAt = {};
    m_lastReplyAt = {};
    m_state = State::Sending;

    LOG_INFO("Start probe, (cidr=192.168.0.0/23 generated={} targets={} excluded_local={})",
             generatedCount,
             m_targets.size(),
             generatedCount - m_targets.size());

    if (!m_targets.empty())
    {
        LOG_DEBUG("ProbeService: target range first={} last={}",
                  m_targets.front().ip,
                  m_targets.back().ip);
    }
}

void ProbeService::sendProbeBatch(IcmpdServiceManager& serviceManager)
{
    const auto now = std::chrono::steady_clock::now();

    std::size_t sentCount = 0;

    while (m_nextSendIndex < m_targets.size() && sentCount < kProbeBatchSize)
    {
        auto& target = m_targets[m_nextSendIndex];

        LOG_TRACE("ProbeService: send echo request dst={} seq={}",
                  target.ip,
                  target.sequence);

        auto packet = buildEchoRequestPacket(target.sequence);
        serviceManager.txRouter().handleIcmpPacket(std::move(packet), target.ip);

        ++m_nextSendIndex;
        ++sentCount;
    }

    m_lastBatchSentAt = now;

    LOG_DEBUG("ProbeService: sent batch count={} progress={}/{}",
              sentCount,
              m_nextSendIndex,
              m_targets.size());
}

void ProbeService::onEchoReply(const ProbeEvent& event)
{
    const auto& srcIp = event.srcIp();

    auto it = m_targetIndexByIp.find(srcIp);
    if (it == m_targetIndexByIp.end())
    {
        LOG_TRACE("ProbeService: ignore echo reply from unknown ip={}", srcIp);
        return;
    }

    if (!canAcceptReply())
    {
        LOG_DEBUG("ProbeService: late echo reply ignored src={} state={}",
                  srcIp,
                  static_cast<int>(m_state));
        return;
    }

    const auto* packet = event.packet();
    if (!packet)
    {
        LOG_WARN("ProbeService: ignore echo reply with null packet src={}", srcIp);
        return;
    }

    auto& target = m_targets[it->second];

    if (packet->identifier() != m_identifier)
    {
        LOG_TRACE("ProbeService: ignore echo reply id mismatch src={} recv={} expected={}",
                  srcIp,
                  packet->identifier(),
                  m_identifier);
        return;
    }

    if (packet->sequence() != target.sequence)
    {
        LOG_TRACE("ProbeService: ignore echo reply seq mismatch src={} recv={} expected={}",
                  srcIp,
                  packet->sequence(),
                  target.sequence);
        return;
    }

    m_lastReplyAt = std::chrono::steady_clock::now();

    if (target.alive)
        return;

    target.alive = true;

    LOG_DEBUG("ProbeService: reachable ip={}", srcIp);
}

void ProbeService::completeProbeSession()
{
    if (m_state != State::WaitingReplies)
    {
        LOG_DEBUG("ProbeService: skip complete state={}", static_cast<int>(m_state));
        return;
    }

    std::vector<std::string> aliveIps;

    for (const auto& target : m_targets)
    {
        if (target.alive)
            aliveIps.push_back(target.ip);
    }

    std::sort(aliveIps.begin(), aliveIps.end(),
              [](const std::string& lhs, const std::string& rhs) {
                  return ipv4ToHostU32(lhs) < ipv4ToHostU32(rhs);
              });

    const auto now = std::chrono::steady_clock::now();

    LOG_INFO("Probe completed, (total={} alive={} dead={} elapsed={}ms)",
             m_targets.size(),
             aliveIps.size(),
             m_targets.size() - aliveIps.size(),
             std::chrono::duration_cast<std::chrono::milliseconds>(
                 now - m_probeStartedAt).count());

    for (const auto& ip : aliveIps)
        LOG_DEBUG("ProbeService: reachable ip={}", ip);

    m_lastProbeCompletedAt = now;
    m_state = State::Idle;
}

bool ProbeService::allProbeSent() const
{
    return m_nextSendIndex >= m_targets.size();
}

bool ProbeService::replyWaitExpired(std::chrono::steady_clock::time_point now) const
{
    using namespace std::chrono;

    const auto idleElapsed = duration_cast<milliseconds>(now - m_lastReplyAt);
    const auto maxElapsed  = duration_cast<milliseconds>(now - m_waitingStartedAt);

    const bool idleExpired = idleElapsed >= kReplyIdleTimeout;
    const bool maxExpired  = maxElapsed  >= kReplyMaxWaitTimeout;

    if (idleExpired || maxExpired)
    {
        std::string reason = idleExpired ? "ReplyIdleTimeout" : "ReplyMaxWaitTimeout";
        
        if (idleExpired && maxExpired) {
            reason = "ReplyIdleTimeout|ReplyMaxWaitTimeout";
        }

        LOG_INFO("Probe stopped, (reason=\"{}\", idle_elapsed={}ms/{}ms, max_elapsed={}ms/{}ms)",
                 reason,
                 idleElapsed.count(),
                 duration_cast<milliseconds>(kReplyIdleTimeout).count(),
                 maxElapsed.count(),
                 duration_cast<milliseconds>(kReplyMaxWaitTimeout).count());
    }

    return idleExpired || maxExpired;
}

bool ProbeService::canAcceptReply() const
{
    return m_state == State::Sending || m_state == State::WaitingReplies;
}

std::unique_ptr<IcmpPacket> ProbeService::buildEchoRequestPacket(std::uint16_t sequence) const
{
    const std::string payload = "nf-icmpd-probe";

    IcmpHeader header = IcmpHeader::buildEchoRequest(m_identifier, sequence);

    auto packet = std::make_unique<IcmpPacket>(std::move(header));
    packet->setPayload(payload.data(), payload.size());

    return packet;
}

std::vector<ProbeService::ProbeTarget>
ProbeService::buildIpv4Targets(const std::string& cidr)
{
    const auto slashPos = cidr.find('/');
    if (slashPos == std::string::npos)
        throw std::invalid_argument("invalid cidr: " + cidr);

    const std::string baseIp = cidr.substr(0, slashPos);
    const int prefix = std::stoi(cidr.substr(slashPos + 1));

    if (prefix < 0 || prefix > 32)
        throw std::invalid_argument("invalid cidr prefix: " + cidr);

    const std::uint32_t base = ipv4ToHostU32(baseIp);

    const std::uint32_t mask =
        prefix == 0 ? 0u : (0xffffffffu << (32 - prefix));

    const std::uint32_t network = base & mask;
    const std::uint32_t broadcast = network | ~mask;

    std::vector<ProbeTarget> targets;

    if (broadcast <= network + 1)
        return targets;

    for (std::uint32_t ip = network + 1; ip < broadcast; ++ip)
    {
        ProbeTarget target;
        target.ip = hostU32ToIpv4(ip);
        targets.push_back(std::move(target));
    }

    return targets;
}

std::uint32_t ProbeService::ipv4ToHostU32(const std::string& ip)
{
    in_addr addr {};
    if (::inet_pton(AF_INET, ip.c_str(), &addr) != 1)
        throw std::invalid_argument("invalid ipv4: " + ip);

    return ntohl(addr.s_addr);
}

std::string ProbeService::hostU32ToIpv4(std::uint32_t value)
{
    in_addr addr {};
    addr.s_addr = htonl(value);

    char buf[INET_ADDRSTRLEN] {};
    if (!::inet_ntop(AF_INET, &addr, buf, sizeof(buf)))
        throw std::runtime_error("inet_ntop failed");

    return std::string(buf);
}

} // namespace nf::icmpd
