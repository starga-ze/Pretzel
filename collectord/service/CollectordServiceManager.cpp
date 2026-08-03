#include "service/CollectordServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace pz::collectord
{

CollectordServiceManager::CollectordServiceManager(CollectordEventFactory* eventFactory, 
        CollectordActionFactory* actionFactory,
        CollectordTxRouter* txRouter, 
        boost::asio::io_context* ioContext) : 
    m_eventFactory(eventFactory), 
    m_actionFactory(actionFactory), 
    m_txRouter(txRouter), 
    m_ioContext(ioContext),
    m_apiService(std::make_unique<ApiService>(*ioContext)),
    m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
    m_heartbeatService(std::make_unique<HeartbeatService>()), 
        m_reloadService(std::make_unique<ReloadService>())
{
}

void CollectordServiceManager::start()
{
    m_bootstrapService->start();
    m_apiService->start();
}

void CollectordServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }

    // All Api periodic work — the one-shot key fetch + collection arming (Setup), the SASE health
    // probe and credential auto-refresh (RunPeriodic) — flows through the event queue: the service
    // decides what is due and returns an event to post, rather than the manager poking it directly.
    postEvent(m_apiService->schedule(now));
}

void CollectordServiceManager::postEvent(std::unique_ptr<CollectordEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void CollectordServiceManager::postAction(std::unique_ptr<CollectordAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void CollectordServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<CollectordEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<CollectordAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

ApiService& CollectordServiceManager::apiService()
{
    return *m_apiService;
}

BootstrapService& CollectordServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

HeartbeatService& CollectordServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

ReloadService& CollectordServiceManager::reloadService()
{
    return *m_reloadService;
}

CollectordTxRouter& CollectordServiceManager::txRouter()
{
    return *m_txRouter;
}

boost::asio::io_context& CollectordServiceManager::ioContext()
{
    return *m_ioContext;
}

}
