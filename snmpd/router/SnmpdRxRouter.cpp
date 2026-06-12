#include "router/SnmpdRxRouter.h"

#include "snmp/SnmpdPacket.h"

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

    // ConfigReload is mapped to a ReloadEvent by the factory and handled in
    // ReloadService — the router stays a pure pass-through.
    std::unique_ptr<SnmpdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void SnmpdRxRouter::handleSnmpPacket(std::unique_ptr<SnmpdPacket> packet)
{
    if (!m_serviceManager)
    {
        LOG_WARN("SnmpdRxRouter: serviceManager is nullptr, dropping scan results");
        return;
    }

    if (!packet)
    {
        LOG_WARN("SnmpdRxRouter: null SnmpdPacket — skipping");
        return;
    }

    LOG_DEBUG("SnmpdRxRouter: posting scan packet devices={}", packet->size());

    m_serviceManager->postEvent(m_eventFactory->create(std::move(packet)));
}

void SnmpdRxRouter::setServiceManager(SnmpdServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace pz::snmpd
