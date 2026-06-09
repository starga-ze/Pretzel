#include "router/SnmpdRxRouter.h"

#include "core/Core.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::snmpd
{

SnmpdRxRouter::SnmpdRxRouter(SnmpdEventFactory* eventFactory) :
    m_eventFactory(eventFactory)
{
}

void SnmpdRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
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

    if (msg->getCmd() == pz::ipc::IpcCmd::ConfigReload)
    {
        LOG_INFO("config reload received — scheduling daemon restart");
        pz::core::Core::scheduleReload();
        return;
    }

    std::unique_ptr<SnmpdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void SnmpdRxRouter::setServiceManager(SnmpdServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace pz::snmpd
