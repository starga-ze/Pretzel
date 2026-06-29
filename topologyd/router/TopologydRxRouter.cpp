#include "router/TopologydRxRouter.h"

#include "service/TopologydServiceManager.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::topologyd
{

TopologydRxRouter::TopologydRxRouter(TopologydEventFactory* eventFactory)
    : m_eventFactory(eventFactory)
{
}

void TopologydRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!m_serviceManager)
    {
        LOG_ERROR("service manager is not initialized");
        return;
    }

    if (!msg)
    {
        LOG_WARN("received null IPC message — skipping");
        return;
    }

    LOG_TRACE("recv (cmd={}, src={})",
              pz::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    // ConfigReload is mapped to a ReloadEvent by the factory and handled in
    // ReloadService — the router stays a pure pass-through.
    auto event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void TopologydRxRouter::setServiceManager(TopologydServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace pz::topologyd
