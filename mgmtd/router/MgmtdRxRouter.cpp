#include "router/MgmtdRxRouter.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::mgmtd
{

MgmtdRxRouter::MgmtdRxRouter(MgmtdEventFactory* eventFactory,
                               MgmtdServiceManager* serviceManager)
    : m_eventFactory(eventFactory),
      m_serviceManager(serviceManager)
{
}

void MgmtdRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!m_serviceManager)
    {
        LOG_ERROR("MgmtdRxRouter: ServiceManager is nullptr");
        return;
    }

    if (!msg)
    {
        LOG_WARN("MgmtdRxRouter: IpcMessage is nullptr");
        return;
    }

    LOG_DEBUG("MgmtdRxRouter: recv cmd={} src={}",
              pz::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    std::unique_ptr<MgmtdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

} // namespace pz::mgmtd
