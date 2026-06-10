#include "service/MgmtdServiceManager.h"

#include "config/Config.h"
#include "util/Logger.h"

#include <chrono>

namespace pz::mgmtd
{

MgmtdServiceManager::MgmtdServiceManager(MgmtdEventFactory* eventFactory,
                                         MgmtdActionFactory* actionFactory,
                                         MgmtdTxRouter* txRouter)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory),
      m_txRouter(txRouter),
      m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
      m_probeService(std::make_unique<ProbeService>()),
      m_heartbeatService(std::make_unique<HeartbeatService>()),
      m_snmpService(std::make_unique<SnmpService>())
{
}

void MgmtdServiceManager::start()
{
    m_metricService.start();
    m_bootstrapService->start();
}

void MgmtdServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }

    m_metricService.tick(now);
}

void MgmtdServiceManager::postEvent(std::unique_ptr<MgmtdEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void MgmtdServiceManager::postAction(std::unique_ptr<MgmtdAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void MgmtdServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<MgmtdEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<MgmtdAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

AuthService& MgmtdServiceManager::authService()
{
    return m_authService;
}

MetricService& MgmtdServiceManager::metricService()
{
    return m_metricService;
}

BootstrapService& MgmtdServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

ProbeService& MgmtdServiceManager::probeService()
{
    return *m_probeService;
}

HeartbeatService& MgmtdServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

SnmpService& MgmtdServiceManager::snmpService()
{
    return *m_snmpService;
}

MgmtdTxRouter& MgmtdServiceManager::txRouter()
{
    return *m_txRouter;
}

std::optional<std::uint32_t> MgmtdServiceManager::aliveDevices() const
{
    const std::int64_t val = m_aliveDevices.load(std::memory_order_relaxed);
    if (val < 0)
    {
        return std::nullopt;
    }

    return static_cast<std::uint32_t>(val);
}

void MgmtdServiceManager::startReload()
{
    m_reloadStartedAt = std::chrono::steady_clock::now();
    m_reloadStatus.store(static_cast<int>(ReloadStatus::Reloading), std::memory_order_release);
    LOG_INFO("reload started");
}

void MgmtdServiceManager::completeReload()
{
    // Invalidate mgmtd's own config cache so the next /api/settings read
    // picks up the values that engined just persisted to disk.
    pz::config::Config::invalidateConfigCache();
    m_reloadStatus.store(static_cast<int>(ReloadStatus::Complete), std::memory_order_release);
    LOG_INFO("reload complete elapsed={}ms", reloadElapsedMs());
}

MgmtdServiceManager::ReloadStatus MgmtdServiceManager::reloadStatus() const
{
    return static_cast<ReloadStatus>(m_reloadStatus.load(std::memory_order_acquire));
}

std::int64_t MgmtdServiceManager::reloadElapsedMs() const
{
    if (reloadStatus() == ReloadStatus::Idle)
        return 0;
    const auto elapsed = std::chrono::steady_clock::now() - m_reloadStartedAt;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

void MgmtdServiceManager::setCommitQueue(std::string snapshotJson)
{
    std::lock_guard<std::mutex> lk(m_commitQueueMutex);
    m_commitQueueSnapshot = std::move(snapshotJson);
}

std::string MgmtdServiceManager::commitQueueSnapshot() const
{
    std::lock_guard<std::mutex> lk(m_commitQueueMutex);
    return m_commitQueueSnapshot;
}

void MgmtdServiceManager::setAliveDevices(std::uint32_t count)
{
    m_aliveDevices.store(static_cast<std::int64_t>(count), std::memory_order_relaxed);
    LOG_DEBUG("aliveDevices updated count={}", count);
}

std::vector<std::string> MgmtdServiceManager::aliveIps() const
{
    std::lock_guard<std::mutex> lock(m_aliveIpsMutex);
    return m_aliveIps;
}

void MgmtdServiceManager::setAliveIps(std::vector<std::string> ips)
{
    std::lock_guard<std::mutex> lock(m_aliveIpsMutex);
    m_aliveIps = std::move(ips);
}

} // namespace pz::mgmtd
