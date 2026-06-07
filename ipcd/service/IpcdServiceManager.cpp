#include "service/IpcdServiceManager.h"

#include "util/Logger.h"

namespace pz::ipcd
{

IpcdServiceManager::IpcdServiceManager(IpcdEventFactory* eventFactory,
                                       IpcdActionFactory* actionFactory,
                                       IpcServerHandler* ipcServerHandler,
                                       IpcdTxRouter* txRouter)
    : m_eventFactory(eventFactory),
      m_actionFactory(actionFactory),
      m_txRouter(txRouter),
      m_bootstrapService(std::make_unique<BootstrapService>(
          m_eventFactory, m_actionFactory, ipcServerHandler))
{
}

void IpcdServiceManager::start()
{
}

void IpcdServiceManager::schedule()
{
}

void IpcdServiceManager::postEvent(std::unique_ptr<IpcdEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void IpcdServiceManager::postAction(std::unique_ptr<IpcdAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void IpcdServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<IpcdEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<IpcdAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

BootstrapService& IpcdServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

IpcdTxRouter& IpcdServiceManager::txRouter()
{
    return *m_txRouter;
}

} // namespace pz::ipcd
