#include "router/AuthdRxRouter.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::authd
{

AuthdRxRouter::AuthdRxRouter(AuthdEventFactory* eventFactory) :
    m_eventFactory(eventFactory)
{
}

void AuthdRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!m_serviceManager)
    {
        LOG_ERROR("ServiceManager is not initialized");
        return;
    }

    if (!msg)
    {
        LOG_WARN("received empty IPC message — skipping");
        return;
    }

    // ConfigReload is mapped to a ReloadEvent by the factory and handled in
    // ReloadService — the router stays a pure pass-through.
    std::unique_ptr<AuthdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void AuthdRxRouter::setServiceManager(AuthdServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace pz::authd
