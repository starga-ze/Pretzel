#include "service/probe/ProbeService.h"

#include "service/EnginedServiceManager.h"
#include "router/EnginedTxRouter.h"

#include "ipc/IpcProtocol.h"
#include "ipc/IpcMessage.h"

#include "db/Database.h"
#include "config/Config.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace pz::engined
{

namespace
{

// Overridable via "service"."probe" in the running-config (section "engined").
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

} // namespace

void ProbeService::start()
{
    m_pending    = false;
    m_lastPollAt = {};
    LOG_INFO("ProbeService (engined) start");
}

std::unique_ptr<EnginedEvent>
ProbeService::schedule(std::chrono::steady_clock::time_point now)
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

    if (m_lastPollAt.time_since_epoch().count() == 0 ||
        now - m_lastPollAt >= pollInterval())
    {
        m_lastPollAt = now;
        return std::make_unique<ProbeEvent>(ProbeEventType::TriggerProbe);
    }

    return nullptr;
}

void ProbeService::handleEvent(EnginedServiceManager& serviceManager,
                               const ProbeEvent& event)
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
        LOG_WARN("unhandled event (type={})",
                 static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void ProbeService::sendProbeRequest(EnginedServiceManager& serviceManager)
{
    const auto flag = pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request);

    pz::ipc::IpcHeader header = pz::ipc::IpcHeader::build(
        pz::ipc::IpcDaemon::Engined,
        pz::ipc::IpcDaemon::Icmpd,
        pz::ipc::IpcCmd::ProbeRequest,
        0,
        flag);

    auto msg = std::make_unique<pz::ipc::IpcMessage>(std::move(header));

    m_pending     = true;
    m_requestedAt = std::chrono::steady_clock::now();

    LOG_DEBUG("sending ProbeRequest to icmpd");

    serviceManager.txRouter().handleIpcMessage(std::move(msg));
}

void ProbeService::onProbeResult(EnginedServiceManager& serviceManager,
                                 const ProbeEvent& event)
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

    LOG_INFO("probe complete (alive={}, total={})", aliveCount, ips.size());

    // Build an IP→MAC map from every device's learned ARP table (probe_devices.
    // arp_entries) so SNMP-less hosts can be tagged with a MAC + OUI vendor (laptop/
    // AP/phone). Empty until the first SNMP scan has run.
    auto& db = pz::db::Database::instance();
    std::unordered_map<std::string, std::string> ipToMac;
    for (const auto& row : db.queryRows("SELECT arp_entries FROM probe_devices "
                                        "WHERE arp_entries IS NOT NULL"))
    {
        if (row.empty())
            continue;
        auto arr = nlohmann::json::parse(row[0], nullptr, false);
        if (!arr.is_array())
            continue;
        for (const auto& e : arr)
        {
            const std::string ip  = e.value("ip", "");
            const std::string mac = e.value("mac", "");
            if (!ip.empty() && !mac.empty())
                ipToMac.emplace(ip, mac);
        }
    }

    // Persist the alive set into probe_devices (engined is the single DB writer).
    // ProbeService owns the ICMP-stage columns (status/mac/host_vendor) AND the row
    // lifecycle: each alive IP is upserted, then IPs that dropped out of the sweep are
    // pruned (taking their SNMP/API columns with them). The SNMP/API columns are left
    // untouched here — ScanService owns them. exec() fails soft when the DB is down.
    auto& vendorResolver = serviceManager.vendorResolver();
    for (const auto& ip : ips)
    {
        std::string mac;
        std::string vendor;
        const auto it = ipToMac.find(ip);
        if (it != ipToMac.end())
        {
            mac    = it->second;
            vendor = vendorResolver.vendorForMac(mac);
        }

        db.exec("INSERT INTO probe_devices (ip, status, mac, host_vendor) "
                "VALUES ($1, 'alive', $2, $3) "
                "ON CONFLICT (ip) DO UPDATE SET status = 'alive', "
                "  mac = EXCLUDED.mac, host_vendor = EXCLUDED.host_vendor, updated_at = now()",
                {ip, mac, vendor});
    }

    // Prune rows whose IP is no longer reachable. Guarded against an empty sweep so a
    // transient zero-alive cycle can't wipe the whole inventory. IPs are validated
    // IPv4 from the ICMP path, safe to inline as a text[] literal.
    if (!ips.empty())
    {
        std::string arr = "{";
        for (size_t i = 0; i < ips.size(); ++i)
        {
            if (i) arr += ',';
            arr += ips[i];
        }
        arr += "}";
        db.exec("DELETE FROM probe_devices WHERE ip <> ALL($1::text[])", {arr});
    }

    serviceManager.setAliveIps(std::move(ips));
}

} // namespace pz::engined
