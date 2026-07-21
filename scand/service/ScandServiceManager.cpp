#include "service/ScandServiceManager.h"

#include "util/Logger.h"

#include <chrono>

namespace pz::scand
{

ScandServiceManager::ScandServiceManager(ScandEventFactory* eventFactory, ScandActionFactory* actionFactory,
                                         ScandTxRouter* txRouter, boost::asio::io_context* ioContext)
    : m_eventFactory(eventFactory), m_actionFactory(actionFactory), m_txRouter(txRouter), m_ioContext(ioContext),
      m_apiService(std::make_unique<ApiService>()),
      m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
      m_heartbeatService(std::make_unique<HeartbeatService>()), m_reloadService(std::make_unique<ReloadService>())
{
}

void ScandServiceManager::start()
{
    m_bootstrapService->start();
    m_apiService->start();
}

void ScandServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }

    // The issued keys live in engined's api_key_state; ask once bootstrap is through, so a test
    // or a collection does not need the operator's password again. Not in start(): the IPC
    // client has not registered yet there, and the request would be dropped on the floor.
    // A config reload restarts the daemon, so this runs again with the new configuration.
    if (!m_keysRequested)
    {
        m_keysRequested = true;
        m_apiService->requestKeys(*this);
    }
}

void ScandServiceManager::postEvent(std::unique_ptr<ScandEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void ScandServiceManager::postAction(std::unique_ptr<ScandAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void ScandServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<ScandEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<ScandAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

ApiService& ScandServiceManager::apiService()
{
    return *m_apiService;
}

BootstrapService& ScandServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

HeartbeatService& ScandServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

ReloadService& ScandServiceManager::reloadService()
{
    return *m_reloadService;
}

ScandTxRouter& ScandServiceManager::txRouter()
{
    return *m_txRouter;
}

boost::asio::io_context& ScandServiceManager::ioContext()
{
    return *m_ioContext;
}

}
