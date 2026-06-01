#include "router/IcmpdRxRouter.h"
#include "util/Logger.h"

namespace nf::icmpd
{

IcmpdRxRouter::IcmpdRxRouter()
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

    LOG_INFO("we are here ");

    std::unique_ptr<nf::event::Event> event = m_eventFactory.create(std::move(msg));

    handleEvent(std::move(event));
}

void IcmpdRxRouter::handleEvent(std::unique_ptr<nf::event::Event> event)
{
    LOG_DEBUG("handle Event");
    m_serviceManager->dispatch(std::move(event));
}

void IcmpdRxRouter::setServiceManager(IcmpdServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace nf::icmpd
