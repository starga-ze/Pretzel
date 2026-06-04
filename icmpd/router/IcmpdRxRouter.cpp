#include "router/IcmpdRxRouter.h"
#include "util/Logger.h"

namespace nf::icmpd
{

IcmpdRxRouter::IcmpdRxRouter(IcmpdEventFactory* eventFactory) :
    m_eventFactory(eventFactory)
{
}

void IcmpdRxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
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

    handleEvent(std::move(event));
}

void IcmpdRxRouter::handleEvent(std::unique_ptr<IcmpdEvent> event)
{
    m_serviceManager->postEvent(std::move(event));
}

void IcmpdRxRouter::setServiceManager(IcmpdServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace nf::icmpd
