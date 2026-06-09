#include "router/TopologydRxRouter.h"

#include "service/TopologydServiceManager.h"
#include "core/Core.h"
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
        LOG_ERROR("TopologydRxRouter: ServiceManager is nullptr");
        return;
    }

    if (!msg)
    {
        LOG_WARN("TopologydRxRouter: IpcMessage is nullptr");
        return;
    }

    LOG_DEBUG("TopologydRxRouter: recv cmd={} src={}",
              pz::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    if (msg->getCmd() == pz::ipc::IpcCmd::ConfigReload)
    {
        LOG_INFO("TopologydRxRouter: ConfigReload received — scheduling restart");
        pz::core::Core::scheduleReload();
        return;
    }

    auto event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void TopologydRxRouter::setServiceManager(TopologydServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace pz::topologyd
