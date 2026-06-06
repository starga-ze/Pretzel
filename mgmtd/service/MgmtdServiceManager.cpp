#include "service/MgmtdServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace nf::mgmtd
{

MgmtdServiceManager::MgmtdServiceManager(MgmtdEventFactory* eventFactory,
                                         MgmtdActionFactory* actionFactory,
                                         MgmtdTxRouter* txRouter)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory),
      m_txRouter(txRouter),
      m_bootstrapService(std::make_unique<MgmtdBootstrapService>(m_eventFactory, m_actionFactory)),
      m_probeService(std::make_unique<MgmtdProbeService>())
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

MgmtdBootstrapService& MgmtdServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

MgmtdProbeService& MgmtdServiceManager::probeService()
{
    return *m_probeService;
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

void MgmtdServiceManager::setAliveDevices(std::uint32_t count)
{
    m_aliveDevices.store(static_cast<std::int64_t>(count), std::memory_order_relaxed);
    LOG_DEBUG("MgmtdServiceManager: aliveDevices updated count={}", count);
}

} // namespace nf::mgmtd
