#include "service/SnmpdServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace nf::snmpd
{

SnmpdServiceManager::SnmpdServiceManager(SnmpdEventFactory* eventFactory,
                                         SnmpdActionFactory* actionFactory,
                                         SnmpdTxRouter* txRouter)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory),
      m_txRouter(txRouter),
      m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
      m_heartbeatService(std::make_unique<HeartbeatService>())
{
}

void SnmpdServiceManager::start()
{
    m_bootstrapService->start();
}

void SnmpdServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }
}

void SnmpdServiceManager::postEvent(std::unique_ptr<SnmpdEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void SnmpdServiceManager::postAction(std::unique_ptr<SnmpdAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void SnmpdServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<SnmpdEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<SnmpdAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

BootstrapService& SnmpdServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

HeartbeatService& SnmpdServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

SnmpdTxRouter& SnmpdServiceManager::txRouter()
{
    return *m_txRouter;
}

} // namespace nf::snmpd
