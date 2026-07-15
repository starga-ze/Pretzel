#include "service/TopologydServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace pz::topologyd
{

TopologydServiceManager::TopologydServiceManager(TopologydEventFactory* eventFactory,
                                                 TopologydActionFactory* actionFactory, TopologydTxRouter* txRouter)
    : m_eventFactory(eventFactory), m_actionFactory(actionFactory), m_txRouter(txRouter),
      m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
      m_heartbeatService(std::make_unique<HeartbeatService>()), m_reloadService(std::make_unique<ReloadService>())
{
}

void TopologydServiceManager::start()
{
    m_bootstrapService->start();
}

void TopologydServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }
}

void TopologydServiceManager::postEvent(std::unique_ptr<TopologydEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void TopologydServiceManager::postAction(std::unique_ptr<TopologydAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void TopologydServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<TopologydEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<TopologydAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

BootstrapService& TopologydServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

HeartbeatService& TopologydServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

ReloadService& TopologydServiceManager::reloadService()
{
    return *m_reloadService;
}

TopologydTxRouter& TopologydServiceManager::txRouter()
{
    return *m_txRouter;
}

}
