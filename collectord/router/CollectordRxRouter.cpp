#include "router/CollectordRxRouter.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::collectord
{

CollectordRxRouter::CollectordRxRouter(CollectordEventFactory* eventFactory) : m_eventFactory(eventFactory)
{
}

void CollectordRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
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

    std::unique_ptr<CollectordEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void CollectordRxRouter::setServiceManager(CollectordServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

}
