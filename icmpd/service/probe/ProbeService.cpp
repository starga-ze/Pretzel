// service/probe/ProbeService.cpp
#include "service/probe/ProbeService.h"

#include "action/IcmpdActionFactory.h"
#include "event/IcmpdEventFactory.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "router/IcmpdTxRouter.h"
#include "service/IcmpdServiceManager.h"
#include "config/Config.h"
#include "util/Logger.h"
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace pz::icmpd
{

namespace
{

// Defaults match the compiled-in values; overridable via "service"."probe" in the
// running-config (section "icmpd").
const nlohmann::json& probeConfig()
{
    return pz::config::Config::serviceSection("icmpd", "probe");
}

std::size_t probeBatchSize()
{
    return probeConfig().value("batch_size", 32);
}

std::chrono::milliseconds probeBatchInterval()
{
    return std::chrono::milliseconds(probeConfig().value("batch_interval_ms", 20));
}

std::chrono::milliseconds probeCycleInterval()
{
    return std::chrono::seconds(probeConfig().value("cycle_interval_sec", 30));
}

std::chrono::milliseconds replyIdleTimeout()
{
    return std::chrono::seconds(probeConfig().value("reply_idle_timeout_sec", 5));
}

std::chrono::milliseconds replyMaxWaitTimeout()
{
    return std::chrono::seconds(probeConfig().value("reply_max_wait_timeout_sec", 15));
}

// Returns comma-separated CIDR list as individual strings.
std::vector<std::string> probeScanCidrs()
{
    const std::string raw = probeConfig().value("scan_cidr", std::string("192.168.0.0/23"));
    std::vector<std::string> result;
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        const auto b = token.find_first_not_of(" \t");
        const auto e = token.find_last_not_of(" \t");
        if (b != std::string::npos)
            result.push_back(token.substr(b, e - b + 1));
    }
    if (result.empty())
        result.push_back("192.168.0.0/23");
    return result;
}

// Comma-separated list of IPs to exclude from probe targets.
std::vector<std::string> probeExcludedIps()
{
    const std::string raw = probeConfig().value("excluded_ips", std::string(""));
    std::vector<std::string> result;
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        // trim whitespace
        const auto b = token.find_first_not_of(" \t");
        const auto e = token.find_last_not_of(" \t");
        if (b != std::string::npos)
            result.push_back(token.substr(b, e - b + 1));
    }
    return result;
}

} // namespace

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
    for (const auto& ip : probeExcludedIps())
        m_localIps.insert(ip);

    LOG_INFO("probe service started");
}

std::unique_ptr<IcmpdEvent> ProbeService::schedule(std::chrono::steady_clock::time_point now)
{
    if (!m_eventFactory)
    {
        LOG_ERROR("EventFactory is not initialized");
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
        if (now - m_lastProbeCompletedAt >= probeCycleInterval())
        {
            return m_eventFactory->create(
                IcmpdEventDomain::Probe,
                static_cast<std::uint32_t>(ProbeEventType::StartProbe));
        }

        return nullptr;
    }

    case State::Sending:
    {
        if (!allProbeSent() && now - m_lastBatchSentAt >= probeBatchInterval())
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
                         replyIdleTimeout()).count(),
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         replyMaxWaitTimeout()).count());
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
        LOG_ERROR("ActionFactory is not initialized");
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

        auto action = m_actionFactory->create(
            IcmpdActionDomain::Probe,
            static_cast<std::uint32_t>(ProbeActionType::SendProbeResult));

        serviceManager.postAction(std::move(action));
        break;
    }

    default:
    {
        LOG_DEBUG("unhandled event type={}",
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
            LOG_DEBUG("skip StartProbe while active state={}",
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
            LOG_DEBUG("skip SendProbeBatch state={}",
                      static_cast<int>(m_state));
            return;
        }

        sendProbeBatch(serviceManager);
        break;
    }

    case ProbeActionType::SendProbeResult:
    {
        sendProbeResult(serviceManager);
        break;
    }

    default:
    {
        LOG_DEBUG("unhandled action type={}",
                  static_cast<std::uint32_t>(action.type()));
        break;
    }
    }
}

void ProbeService::beginProbeSession()
{
    m_identifier = static_cast<std::uint16_t>(::getpid() & 0xffff);

    // Build targets from all configured CIDRs, deduplicating by IP.
    const auto cidrs = probeScanCidrs();
    std::unordered_map<std::string, bool> seen;
    m_targets.clear();

    for (const auto& cidr : cidrs)
    {
        auto cidrTargets = buildIpv4Targets(cidr);
        for (auto& t : cidrTargets)
        {
            if (seen.emplace(t.ip, true).second)
                m_targets.push_back(std::move(t));
        }
    }

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
    m_lastBatchSentAt = now - probeBatchInterval();
    m_waitingStartedAt = {};
    m_lastReplyAt = {};
    m_state = State::Sending;

    // Build comma-joined CIDR string for the log line.
    std::string cidrList;
    for (std::size_t i = 0; i < cidrs.size(); ++i)
    {
        if (i) cidrList += ',';
        cidrList += cidrs[i];
    }

    LOG_INFO("Start probe, (cidr={} generated={} targets={} excluded_local={})",
             cidrList,
             generatedCount,
             m_targets.size(),
             generatedCount - m_targets.size());
}

void ProbeService::sendProbeBatch(IcmpdServiceManager& serviceManager)
{
    const auto now = std::chrono::steady_clock::now();

    std::size_t sentCount = 0;

    while (m_nextSendIndex < m_targets.size() && sentCount < probeBatchSize())
    {
        auto& target = m_targets[m_nextSendIndex];

        LOG_TRACE("send echo request dst={} seq={}",
                  target.ip,
                  target.sequence);

        auto packet = buildEchoRequestPacket(target.sequence);
        serviceManager.txRouter().handleIcmpPacket(std::move(packet), target.ip);

        ++m_nextSendIndex;
        ++sentCount;
    }

    m_lastBatchSentAt = now;

    LOG_DEBUG("sent batch count={} progress={}/{}",
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
        LOG_TRACE("ignore echo reply from unknown ip={}", srcIp);
        return;
    }

    if (!canAcceptReply())
    {
        LOG_DEBUG("late echo reply ignored src={} state={}",
                  srcIp,
                  static_cast<int>(m_state));
        return;
    }

    const auto* packet = event.packet();
    if (!packet)
    {
        LOG_WARN("ignore echo reply with null packet src={}", srcIp);
        return;
    }

    auto& target = m_targets[it->second];

    if (packet->identifier() != m_identifier)
    {
        LOG_TRACE("ignore echo reply id mismatch src={} recv={} expected={}",
                  srcIp,
                  packet->identifier(),
                  m_identifier);
        return;
    }

    if (packet->sequence() != target.sequence)
    {
        LOG_TRACE("ignore echo reply seq mismatch src={} recv={} expected={}",
                  srcIp,
                  packet->sequence(),
                  target.sequence);
        return;
    }

    m_lastReplyAt = std::chrono::steady_clock::now();

    if (target.alive)
        return;

    target.alive = true;

    LOG_TRACE("reachable ip={}", srcIp);
}

void ProbeService::completeProbeSession()
{
    if (m_state != State::WaitingReplies)
    {
        LOG_DEBUG("skip complete state={}", static_cast<int>(m_state));
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

    m_lastAliveCount = static_cast<std::uint32_t>(aliveIps.size());

    LOG_INFO("Probe completed, (total={} alive={} dead={} elapsed={}ms)",
             m_targets.size(),
             aliveIps.size(),
             m_targets.size() - aliveIps.size(),
             std::chrono::duration_cast<std::chrono::milliseconds>(
                 now - m_probeStartedAt).count());

    for (const auto& ip : aliveIps)
        LOG_TRACE("reachable ip={}", ip);

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

    const bool idleExpired = idleElapsed >= replyIdleTimeout();
    const bool maxExpired  = maxElapsed  >= replyMaxWaitTimeout();

    if (idleExpired || maxExpired)
    {
        std::string reason = idleExpired ? "ReplyIdleTimeout" : "ReplyMaxWaitTimeout";
        
        if (idleExpired && maxExpired) {
            reason = "ReplyIdleTimeout|ReplyMaxWaitTimeout";
        }

        LOG_INFO("Probe stopped, (reason=\"{}\", idle_elapsed={}ms/{}ms, max_elapsed={}ms/{}ms)",
                 reason,
                 idleElapsed.count(),
                 duration_cast<milliseconds>(replyIdleTimeout()).count(),
                 maxElapsed.count(),
                 duration_cast<milliseconds>(replyMaxWaitTimeout()).count());
    }

    return idleExpired || maxExpired;
}

bool ProbeService::canAcceptReply() const
{
    return m_state == State::Sending || m_state == State::WaitingReplies;
}

std::unique_ptr<IcmpPacket> ProbeService::buildEchoRequestPacket(std::uint16_t sequence) const
{
    const std::string payload = "pz-icmpd-probe";

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

void ProbeService::sendProbeResult(IcmpdServiceManager& serviceManager)
{
    // payload: JSON {"alive": N, "ips": ["1.2.3.4", ...]}
    nlohmann::json payload;
    payload["alive"] = m_lastAliveCount;
    nlohmann::json ips = nlohmann::json::array();
    for (const auto& t : m_targets)
    {
        if (t.alive)
            ips.push_back(t.ip);
    }
    payload["ips"] = std::move(ips);

    const std::string payloadStr = payload.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Icmpd);
    msg->setDst(pz::ipc::IpcDaemon::Mgmtd);
    msg->setCmd(pz::ipc::IpcCmd::ProbeResult);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(payloadStr.data()), payloadStr.size());

    LOG_INFO("sending ProbeResult to mgmtd alive={}", m_lastAliveCount);

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
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

} // namespace pz::icmpd
