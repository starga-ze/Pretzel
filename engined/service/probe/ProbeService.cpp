#include "service/probe/ProbeService.h"

#include "router/EnginedTxRouter.h"
#include "service/EnginedServiceManager.h"

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include "config/Config.h"
#include "db/Database.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace pz::engined
{

namespace
{

std::chrono::milliseconds pollInterval()
{
    const auto& p = pz::config::Config::serviceSection("engined", "probe");
    return std::chrono::seconds(p.value("poll_interval_sec", 30));
}

std::chrono::milliseconds responseTimeout()
{
    const auto& p = pz::config::Config::serviceSection("engined", "probe");
    return std::chrono::seconds(p.value("response_timeout_sec", 20));
}

}

void ProbeService::start()
{
    m_pending = false;
    m_lastPollAt = {};
    LOG_INFO("ProbeService (engined) start");
}

std::unique_ptr<EnginedEvent> ProbeService::schedule(std::chrono::steady_clock::time_point now)
{
    if (m_pending)
    {
        if (now - m_requestedAt >= responseTimeout())
        {
            LOG_WARN("ProbeResult timed out — clearing pending");
            m_pending = false;
        }
        return nullptr;
    }

    if (m_lastPollAt.time_since_epoch().count() == 0 || now - m_lastPollAt >= pollInterval())
    {
        m_lastPollAt = now;
        return std::make_unique<ProbeEvent>(ProbeEventType::TriggerProbe);
    }

    return nullptr;
}

void ProbeService::handleEvent(EnginedServiceManager& serviceManager, const ProbeEvent& event)
{
    switch (event.type())
    {
    case ProbeEventType::TriggerProbe:
        sendProbeRequest(serviceManager);
        break;

    case ProbeEventType::ReceiveProbeResult:
        onProbeResult(serviceManager, event);
        break;

    default:
        LOG_WARN("unhandled event (type={})", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void ProbeService::sendProbeRequest(EnginedServiceManager& serviceManager)
{
    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(pz::ipc::IpcDaemon::Engined, pz::ipc::IpcDaemon::Icmpd,
                                                          pz::ipc::IpcCmd::ProbeRequest, 0, flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));

    m_pending = true;
    m_requestedAt = std::chrono::steady_clock::now();

    LOG_DEBUG("sending ProbeRequest to icmpd");

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void ProbeService::onProbeResult(EnginedServiceManager& serviceManager, const ProbeEvent& event)
{
    m_pending = false;

    const pz::ipc::IpcMessage* msg = event.message();
    if (!msg || msg->getPayload().empty())
    {
        LOG_WARN("empty ProbeResult — keeping previous alive snapshot");
        return;
    }

    const auto& pl = msg->getPayload();
    std::vector<std::string> ips;
    std::uint32_t aliveCount = 0;

    try
    {
        const std::string json(reinterpret_cast<const char*>(pl.data()), pl.size());
        const auto root = nlohmann::json::parse(json);
        aliveCount = root.value("alive", 0u);
        for (const auto& ip : root.value("ips", nlohmann::json::array()))
            ips.push_back(ip.get<std::string>());
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse ProbeResult (error={})", e.what());
        return;
    }

    LOG_INFO("probe complete (alive={}, received_ips={})", aliveCount, ips.size());

    // Keep the device projection in sync with config, then reflect ICMP reachability into status.
    projectInventory();

    auto& db = pz::db::Database::instance();
    const std::string alive = nlohmann::json(ips).dump();   // JSON array of alive IPs

    // Enabled direct (IP-based) objects that did not answer → down; answered → active.
    db.exec("UPDATE devices SET status='down' "
            "WHERE device_type='ngfw' "
            "AND target <> ALL(ARRAY(SELECT jsonb_array_elements_text($1::jsonb)))",
            {alive});
    db.exec("UPDATE devices SET status='active', last_seen=now() "
            "WHERE device_type='ngfw' "
            "AND target = ANY(ARRAY(SELECT jsonb_array_elements_text($1::jsonb)))",
            {alive});

    serviceManager.setAliveIps(std::move(ips));
}

void ProbeService::projectInventory()
{
    const auto& site = pz::config::Config::serviceSection("engined", "site");
    const auto targets = site.value("devices", nlohmann::json::array());

    auto& db = pz::db::Database::instance();
    nlohmann::json ids = nlohmann::json::array();

    for (const auto& t : targets)
    {
        if (!t.is_object())
            continue;

        const std::string target = t.value("target", std::string());
        // Object identity (a UUID string); tolerate the legacy `uuid`/`id` keys, then fall back
        // to target for pre-identity configs so projection still works.
        std::string oid = t.value("oid", t.value("uuid", t.value("id", std::string())));
        if (oid.empty())
            oid = target;
        if (oid.empty())
            continue;

        const std::string deviceType =
            t.value("device_type", std::string()) == "prisma_access" ? "prisma_access" : "ngfw";

        db.exec("INSERT INTO devices (oid, site, device_type, target, name, description) "
                "VALUES ($1,$2,$3,$4,$5,$6) "
                "ON CONFLICT (oid) DO UPDATE SET site=EXCLUDED.site, device_type=EXCLUDED.device_type, "
                "target=EXCLUDED.target, name=EXCLUDED.name, description=EXCLUDED.description, "
                "updated_at=now()",
                {oid, t.value("site", std::string()), deviceType, target,
                 t.value("name", std::string()), t.value("description", std::string())});

        ids.push_back(oid);
    }

    // Prune rows whose object is no longer in config (empty ids → clears the table).
    db.exec("DELETE FROM devices WHERE oid <> ALL(ARRAY(SELECT jsonb_array_elements_text($1::jsonb)))",
            {ids.dump()});
}

}
