#include "router/IcmpdRxRouter.h"
#include "util/Logger.h"

namespace nf::icmpd
{

IcmpdRxRouter::IcmpdRxRouter(IcmpdEventFactory* eventFactory) :
    m_eventFactory(eventFactory)
{
}

void IcmpdRxRouter::handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
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

    std::unique_ptr<IcmpdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void IcmpdRxRouter::handleIcmpPacket(const std::string& srcIp,
                                     std::unique_ptr<IcmpPacket> packet)
{
    if (!packet)
    {
        LOG_WARN("IcmpdRxRouter: ICMP packet is nullptr src={}", srcIp);
        return;
    }

    // TODO:
    // 여기서 ProbeEvent로 변환해서 ServiceManager에 postEvent 하면 됨.
    // 예:
    // auto event = m_eventFactory->createIcmpPacketEvent(srcIp, std::move(packet));
    // m_serviceManager->postEvent(std::move(event));
}

void IcmpdRxRouter::setServiceManager(IcmpdServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace nf::icmpd
