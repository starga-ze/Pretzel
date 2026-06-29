#include "router/ScandRxRouter.h"

#include "snmp/SnmpPacket.h"
#include "api/ApiPacket.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::scand
{

ScandRxRouter::ScandRxRouter(ScandEventFactory* eventFactory) :
    m_eventFactory(eventFactory)
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

    // ConfigReload is mapped to a ReloadEvent by the factory and handled in
    // ReloadService — the router stays a pure pass-through.
    std::unique_ptr<ScandEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void ScandRxRouter::handleSnmpPacket(std::unique_ptr<SnmpPacket> packet)
{
    if (!m_serviceManager)
    {
        LOG_WARN("serviceManager is nullptr, dropping SNMP scan results");
        return;
    }

    if (!packet)
    {
        LOG_WARN("null SnmpPacket — skipping");
        return;
    }

    LOG_DEBUG("posting SNMP scan packet (devices={})", packet->size());

    m_serviceManager->postEvent(m_eventFactory->create(std::move(packet)));
}

void ScandRxRouter::handleApiPacket(std::unique_ptr<ApiPacket> packet)
{
    if (!m_serviceManager)
    {
        LOG_WARN("serviceManager is nullptr, dropping API scan results");
        return;
    }

    if (!packet)
    {
        LOG_WARN("null ApiPacket — skipping");
        return;
    }

    LOG_DEBUG("posting API scan packet (devices={})", packet->size());

    m_serviceManager->postEvent(m_eventFactory->create(std::move(packet)));
}

void ScandRxRouter::setServiceManager(ScandServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace pz::scand
