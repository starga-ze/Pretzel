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
        LOG_ERROR("Mgmtd RxRouter: service manager is not initialized");
        return;
    }

    if (!msg)
    {
        LOG_WARN("Mgmtd RxRouter: received null IPC message — skipping");
        return;
    }

    LOG_DEBUG("recv cmd={} src={}",
              pz::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    if (msg->getCmd() == pz::ipc::IpcCmd::ConfigReloadResponse)
    {
        LOG_INFO("config reload acknowledged by engined");
        m_serviceManager->completeReload();
        return;
    }

    std::unique_ptr<MgmtdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

} // namespace pz::mgmtd
