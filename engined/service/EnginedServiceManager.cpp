#include "service/EnginedServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace nf::engined
{

EnginedServiceManager::EnginedServiceManager(EnginedEventFactory* eventFactory,
                                             EnginedActionFactory* actionFactory,
                                             EnginedTxRouter* txRouter)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory),
      m_txRouter(txRouter),
      m_bootstrapService(std::make_unique<EnginedBootstrapService>(m_eventFactory, m_actionFactory))
{
}

void EnginedServiceManager::start()
{
    m_bootstrapService->start();
}

void EnginedServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }
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

EnginedBootstrapService& EnginedServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

EnginedTxRouter& EnginedServiceManager::txRouter()
{
    return *m_txRouter;
}

} // namespace nf::engined
