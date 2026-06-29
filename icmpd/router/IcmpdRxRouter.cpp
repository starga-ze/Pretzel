#include "router/IcmpdRxRouter.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::icmpd
{

IcmpdRxRouter::IcmpdRxRouter(IcmpdEventFactory* eventFactory) :
    m_eventFactory(eventFactory)
{
}

void IcmpdRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
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
    std::unique_ptr<IcmpdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void IcmpdRxRouter::handleIcmpPacket(const std::string& srcIp,
                                     std::unique_ptr<IcmpPacket> packet)
{
    if (!packet)
    {
        LOG_WARN("ICMP packet is nullptr (src={})", srcIp);
        return;
    }

    auto event = m_eventFactory->create(srcIp, std::move(packet));
    m_serviceManager->postEvent(std::move(event));
}

void IcmpdRxRouter::setServiceManager(IcmpdServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace pz::icmpd
