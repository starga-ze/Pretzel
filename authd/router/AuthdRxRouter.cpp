#include "router/AuthdRxRouter.h"
#include "util/Logger.h"

namespace nf::authd
{

AuthdRxRouter::AuthdRxRouter(AuthdEventFactory* eventFactory) :
    m_eventFactory(eventFactory)
{
}

void AuthdRxRouter::handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
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

    std::unique_ptr<AuthdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void AuthdRxRouter::setServiceManager(AuthdServiceManager* serviceManager)
{
    m_serviceManager = serviceManager;
}

} // namespace nf::authd
