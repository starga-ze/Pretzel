#include "service/ProbedServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace pz::probed
{

ProbedServiceManager::ProbedServiceManager(ProbedEventFactory* eventFactory, ProbedActionFactory* actionFactory,
                                         ProbedTxRouter* txRouter, boost::asio::io_context* ioContext)
    : m_eventFactory(eventFactory), m_actionFactory(actionFactory), m_txRouter(txRouter), m_ioContext(ioContext),
      m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
      m_icmpService(std::make_unique<IcmpService>(m_eventFactory, m_actionFactory)),
      m_heartbeatService(std::make_unique<HeartbeatService>()), m_reloadService(std::make_unique<ReloadService>())
{
}

void ProbedServiceManager::start()
{
    m_bootstrapService->start();
    m_icmpService->start();
}

void ProbedServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }

    if (auto event = m_icmpService->schedule(now))
    {
        postEvent(std::move(event));
    }
}

void ProbedServiceManager::postEvent(std::unique_ptr<ProbedEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void ProbedServiceManager::postAction(std::unique_ptr<ProbedAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void ProbedServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<ProbedEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<ProbedAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

BootstrapService& ProbedServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

IcmpService& ProbedServiceManager::icmpService()
{
    return *m_icmpService;
}

HeartbeatService& ProbedServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

ReloadService& ProbedServiceManager::reloadService()
{
    return *m_reloadService;
}

ProbedTxRouter& ProbedServiceManager::txRouter()
{
    return *m_txRouter;
}

boost::asio::io_context& ProbedServiceManager::ioContext()
{
    return *m_ioContext;
}

}
