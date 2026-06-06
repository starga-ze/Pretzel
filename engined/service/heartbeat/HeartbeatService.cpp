#include "service/heartbeat/HeartbeatService.h"

#include "service/EnginedServiceManager.h"
#include "router/EnginedTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace nf::engined
{

constexpr auto kPollInterval    = std::chrono::seconds(5);
constexpr auto kResponseTimeout = std::chrono::milliseconds(2000);

const std::vector<nf::ipc::IpcDaemon>& HeartbeatService::targets()
{
    static const std::vector<nf::ipc::IpcDaemon> kTargets = {
        nf::ipc::IpcDaemon::Authd,
        nf::ipc::IpcDaemon::Icmpd,
        nf::ipc::IpcDaemon::Snmpd,
        nf::ipc::IpcDaemon::Topologyd,
        nf::ipc::IpcDaemon::Mgmtd,
    };
    return kTargets;
}

void HeartbeatService::start()
{
    for (const auto daemon : targets())
    {
        m_daemonMap[daemon] = DaemonEntry{};
    }

    LOG_INFO("HeartbeatService start, targets={}", targets().size());
}

std::unique_ptr<EnginedEvent>
HeartbeatService::schedule(std::chrono::steady_clock::time_point now)
{
    if (m_roundActive)
    {
        if (now - m_roundStartedAt >= kResponseTimeout)
        {
            LOG_DEBUG("HeartbeatService: response timeout, finalizing round");
            markTimeoutsAsDead();
            m_roundActive  = false;
            m_pendingCount = 0;
            return std::make_unique<HeartbeatEvent>(HeartbeatEventType::SendHeartbeatResult);
        }
        return nullptr;
    }

    if (m_lastPollAt.time_since_epoch().count() == 0 ||
        now - m_lastPollAt >= kPollInterval)
    {
        m_lastPollAt = now;
        return std::make_unique<HeartbeatEvent>(HeartbeatEventType::SendHeartbeatRequests);
    }

    return nullptr;
}

void HeartbeatService::handleEvent(EnginedServiceManager& serviceManager,
                                   const HeartbeatEvent& event)
{
    switch (event.type())
    {
    case HeartbeatEventType::SendHeartbeatRequests:
    {
        m_roundActive    = true;
        m_roundStartedAt = std::chrono::steady_clock::now();
        m_pendingCount   = 0;

        for (auto& [daemon, entry] : m_daemonMap)
        {
            entry.pending   = true;
            entry.alive     = false;
            entry.latencyMs = -1;
            entry.sentAt    = m_roundStartedAt;
            ++m_pendingCount;

            serviceManager.postAction(std::make_unique<HeartbeatAction>(
                HeartbeatActionType::SendHeartbeatRequest, daemon));
        }

        LOG_DEBUG("HeartbeatService: sent requests to {} daemons", m_pendingCount);
        break;
    }

    case HeartbeatEventType::ReceiveHeartbeatResponse:
    {
        const auto* msg = event.message();
        if (!msg)
        {
            LOG_WARN("HeartbeatService: ReceiveHeartbeatResponse has empty message");
            return;
        }

        onHeartbeatResponse(serviceManager, *msg);
        break;
    }

    case HeartbeatEventType::SendHeartbeatResult:
    {
        serviceManager.postAction(std::make_unique<HeartbeatAction>(
            HeartbeatActionType::SendHeartbeatResult, buildResultJson()));
        break;
    }

    default:
        LOG_WARN("HeartbeatService: unhandled event type={}",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void HeartbeatService::handleAction(EnginedServiceManager& serviceManager,
                                    const HeartbeatAction& action)
{
    switch (action.type())
    {
    case HeartbeatActionType::SendHeartbeatRequest:
    {
        const auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

        nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Engined,
            action.dst(),
            nf::ipc::IpcCmd::HeartbeatRequest,
            0,
            flag);

        auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));

        LOG_DEBUG("HeartbeatService: Tx HeartbeatRequest dst={}",
                  nf::ipc::IpcProtocol::daemonToStr(action.dst()));

        serviceManager.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    case HeartbeatActionType::SendHeartbeatResult:
    {
        const auto& jsonStr = action.resultJson();

        const auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

        nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Engined,
            nf::ipc::IpcDaemon::Mgmtd,
            nf::ipc::IpcCmd::HeartbeatResult,
            0,
            flag);

        auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
        msg->setPayload(
            reinterpret_cast<const std::uint8_t*>(jsonStr.data()),
            jsonStr.size());

        LOG_DEBUG("HeartbeatService: Tx HeartbeatResult to mgmtd");

        serviceManager.txRouter().handleIpcMessage(std::move(msg));
        break;
    }

    default:
        LOG_WARN("HeartbeatService: unhandled action type={}",
                 static_cast<std::uint32_t>(action.type()));
        break;
    }
}

void HeartbeatService::onHeartbeatResponse(EnginedServiceManager& serviceManager,
                                           const nf::ipc::IpcMessage& msg)
{
    const nf::ipc::IpcDaemon src = msg.getSrc();

    auto it = m_daemonMap.find(src);
    if (it == m_daemonMap.end())
    {
        LOG_WARN("HeartbeatService: unexpected response from daemon={}",
                 nf::ipc::IpcProtocol::daemonToStr(src));
        return;
    }

    auto& entry = it->second;

    if (!entry.pending)
    {
        LOG_DEBUG("HeartbeatService: duplicate response from daemon={}",
                  nf::ipc::IpcProtocol::daemonToStr(src));
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - entry.sentAt).count();

    entry.pending   = false;
    entry.alive     = true;
    entry.latencyMs = latency;
    --m_pendingCount;

    LOG_DEBUG("HeartbeatService: response from {} latency={}ms",
              nf::ipc::IpcProtocol::daemonToStr(src), latency);

    if (m_pendingCount <= 0)
    {
        m_roundActive  = false;
        m_pendingCount = 0;
        serviceManager.postEvent(
            std::make_unique<HeartbeatEvent>(HeartbeatEventType::SendHeartbeatResult));
    }
}

void HeartbeatService::markTimeoutsAsDead()
{
    for (auto& [daemon, entry] : m_daemonMap)
    {
        if (entry.pending)
        {
            entry.pending   = false;
            entry.alive     = false;
            entry.latencyMs = -1;
            LOG_DEBUG("HeartbeatService: daemon={} timed out",
                      nf::ipc::IpcProtocol::daemonToStr(daemon));
        }
    }
}

std::string HeartbeatService::buildResultJson() const
{
    using json = nlohmann::json;

    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    json root;
    root["timestamp_ms"] = nowMs;
    root["daemons"]      = json::array();

    static const std::vector<nf::ipc::IpcDaemon> kOrder = {
        nf::ipc::IpcDaemon::Authd,
        nf::ipc::IpcDaemon::Icmpd,
        nf::ipc::IpcDaemon::Snmpd,
        nf::ipc::IpcDaemon::Topologyd,
        nf::ipc::IpcDaemon::Mgmtd,
    };

    for (const auto daemon : kOrder)
    {
        const auto it = m_daemonMap.find(daemon);

        json entry;
        entry["name"] = nf::ipc::IpcProtocol::daemonToStr(daemon);

        if (it != m_daemonMap.end() && it->second.alive)
        {
            entry["status"]     = "alive";
            entry["latency_ms"] = it->second.latencyMs;
        }
        else
        {
            entry["status"]     = "dead";
            entry["latency_ms"] = nullptr;
        }

        root["daemons"].push_back(std::move(entry));
    }

    return root.dump();
}

} // namespace nf::engined
