#include "router/ScandRxRouter.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::scand
{

ScandRxRouter::ScandRxRouter(ScandEventFactory* eventFactory) : m_eventFactory(eventFactory)
{
}

void ScandRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
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

    std::unique_ptr<ScandEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void ScandRxRouter::setServiceManager(ScandServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

}
