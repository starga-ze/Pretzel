#include "service/IcmpdServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace nf::icmpd
{

IcmpdServiceManager::IcmpdServiceManager(IcmpdEventFactory* eventFactory, IcmpdActionFactory* actionFactory) : 
    m_eventFactory(eventFactory),
    m_actionFactory(actionFactory),
    m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory))
{
}

void IcmpdServiceManager::start()
{
    m_bootstrapService->start();
}

void IcmpdServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }

    // postEvent(m_probeService->schedule(now));
    // postEvent(m_scanService->schedule(now));
}

void IcmpdServiceManager::postEvent(std::unique_ptr<IcmpdEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void IcmpdServiceManager::execute()
{
    while (!m_eventQueue.empty())
    {
        std::unique_ptr<IcmpdEvent> event = std::move(m_eventQueue.front());
        m_eventQueue.pop();

        event->dispatch(*this);
    }
}

BootstrapService& IcmpdServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

} // namespace nf::icmpd
