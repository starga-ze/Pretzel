#include "router/SnmpdRxRouter.h"
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
        LOG_ERROR("ServiceManager is nullptr");
        return;
    }

    if (!msg)
    {
        LOG_WARN("IpcMessage is empty");
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
