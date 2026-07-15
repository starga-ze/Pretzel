#include "service/MgmtdServiceManager.h"

#include "config/Config.h"
#include "util/Logger.h"

#include <chrono>

namespace pz::mgmtd
{

MgmtdServiceManager::MgmtdServiceManager(MgmtdEventFactory* eventFactory, MgmtdActionFactory* actionFactory,
                                         MgmtdTxRouter* txRouter)
    : m_eventFactory(eventFactory), m_actionFactory(actionFactory), m_txRouter(txRouter),
      m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
      m_heartbeatService(std::make_unique<HeartbeatService>()), m_deviceService(std::make_unique<DeviceService>())
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

HeartbeatService& MgmtdServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

DeviceService& MgmtdServiceManager::deviceService()
{
    return *m_deviceService;
}

WebService& MgmtdServiceManager::webService()
{
    return m_webService;
}

MgmtdTxRouter& MgmtdServiceManager::txRouter()
{
    return *m_txRouter;
}

void MgmtdServiceManager::startReload()
{
    m_reloadStartedAt = std::chrono::steady_clock::now();
    m_reloadStatus.store(static_cast<int>(ReloadStatus::Reloading), std::memory_order_release);
    LOG_INFO("reload started");
}

void MgmtdServiceManager::completeReload()
{
    pz::config::Config::invalidateConfigCache();
    m_reloadStatus.store(static_cast<int>(ReloadStatus::Complete), std::memory_order_release);
    LOG_INFO("reload complete (elapsed_ms={})", reloadElapsedMs());
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
    m_commitQueueSnapshot = std::move(snapshotJson);
}

std::string MgmtdServiceManager::commitQueueSnapshot() const
{
    return m_commitQueueSnapshot;
}

void MgmtdServiceManager::setSsoResult(std::uint32_t ticket, std::string resultJson)
{
    if (m_ssoResults.size() > 256)
    {
        m_ssoResults.clear();
    }
    m_ssoResults[ticket] = std::move(resultJson);
}

std::optional<std::string> MgmtdServiceManager::takeSsoResult(std::uint32_t ticket)
{
    auto it = m_ssoResults.find(ticket);
    if (it == m_ssoResults.end())
    {
        return std::nullopt;
    }
    std::string out = std::move(it->second);
    m_ssoResults.erase(it);
    return out;
}

}
