#include "service/EnginedServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace pz::engined
{

EnginedServiceManager::EnginedServiceManager(EnginedEventFactory* eventFactory,
                                             EnginedActionFactory* actionFactory,
                                             EnginedTxRouter* txRouter)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory),
      m_txRouter(txRouter),
      m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
      m_commitService(std::make_unique<CommitService>()),
      m_heartbeatService(std::make_unique<HeartbeatService>()),
      m_scanService(std::make_unique<ScanService>()),
      m_adminService(std::make_unique<AdminService>())
{
}

void EnginedServiceManager::start()
{
    m_bootstrapService->start();
    m_heartbeatService->start();
}

void EnginedServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }

    postEvent(m_heartbeatService->schedule(now));
}

void EnginedServiceManager::postEvent(std::unique_ptr<EnginedEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void EnginedServiceManager::postAction(std::unique_ptr<EnginedAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void EnginedServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<EnginedEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<EnginedAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

BootstrapService& EnginedServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

CommitService& EnginedServiceManager::commitService()
{
    return *m_commitService;
}

HeartbeatService& EnginedServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

ScanService& EnginedServiceManager::scanService()
{
    return *m_scanService;
}

AdminService& EnginedServiceManager::adminService()
{
    return *m_adminService;
}

EnginedTxRouter& EnginedServiceManager::txRouter()
{
    return *m_txRouter;
}

EnginedEventFactory* EnginedServiceManager::eventFactory()
{
    return m_eventFactory;
}

EnginedActionFactory* EnginedServiceManager::actionFactory()
{
    return m_actionFactory;
}

} // namespace pz::engined
