#include "router/EnginedRxRouter.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace nf::engined
{

EnginedRxRouter::EnginedRxRouter(EnginedEventFactory* eventFactory,
                                  EnginedServiceManager* serviceManager)
    : m_eventFactory(eventFactory),
      m_serviceManager(serviceManager)
{
}

void EnginedRxRouter::handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!m_serviceManager)
    {
        LOG_ERROR("EnginedRxRouter: ServiceManager is nullptr");
        return;
    }

    if (!msg)
    {
        LOG_WARN("EnginedRxRouter: IpcMessage is nullptr");
        return;
    }

    LOG_DEBUG("EnginedRxRouter: recv cmd={} src={}",
              nf::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              nf::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    std::unique_ptr<EnginedEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

} // namespace nf::engined
