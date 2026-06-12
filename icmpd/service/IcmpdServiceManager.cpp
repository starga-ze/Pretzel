#include "service/IcmpdServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace pz::icmpd
{

IcmpdServiceManager::IcmpdServiceManager(IcmpdEventFactory* eventFactory, 
        IcmpdActionFactory* actionFactory, 
        IcmpdTxRouter* txRouter) : 
    m_eventFactory(eventFactory),
    m_actionFactory(actionFactory),
    m_txRouter(txRouter),
    m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
    m_probeService(std::make_unique<ProbeService>(m_eventFactory, m_actionFactory)),
    m_heartbeatService(std::make_unique<HeartbeatService>()),
    m_reloadService(std::make_unique<ReloadService>())
{
}

void IcmpdServiceManager::start()
{
    m_bootstrapService->start();
    m_probeService->start();
}

void IcmpdServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }

    if (auto event = m_probeService->schedule(now))
    {
        postEvent(std::move(event));
    }
}

void IcmpdServiceManager::postEvent(std::unique_ptr<IcmpdEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void IcmpdServiceManager::postAction(std::unique_ptr<IcmpdAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void IcmpdServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<IcmpdEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<IcmpdAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

BootstrapService& IcmpdServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

ProbeService& IcmpdServiceManager::probeService()
{
    return *m_probeService;
}

HeartbeatService& IcmpdServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

ReloadService& IcmpdServiceManager::reloadService()
{
    return *m_reloadService;
}

IcmpdTxRouter& IcmpdServiceManager::txRouter()
{
    return *m_txRouter;
}

} // namespace pz::icmpd
