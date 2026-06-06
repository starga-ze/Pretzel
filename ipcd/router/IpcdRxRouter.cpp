#include "router/IpcdRxRouter.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace nf::ipcd
{

IpcdRxRouter::IpcdRxRouter(IpcdEventFactory* eventFactory,
                            IpcdServiceManager* serviceManager,
                            IpcdTxRouter* txRouter)
    : m_eventFactory(eventFactory),
      m_serviceManager(serviceManager),
      m_txRouter(txRouter)
{
}

void IpcdRxRouter::handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!m_serviceManager || !m_txRouter)
    {
        LOG_ERROR("IpcdRxRouter: dependencies not ready serviceManager={} txRouter={}",
                  static_cast<bool>(m_serviceManager),
                  static_cast<bool>(m_txRouter));
        return;
    }

    if (!msg)
    {
        LOG_WARN("IpcdRxRouter: IpcMessage is nullptr");
        return;
    }

    LOG_DEBUG("IpcdRxRouter: recv cmd={} src={}",
              nf::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              nf::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    const nf::ipc::IpcCmd cmd = msg->getCmd();

    switch (cmd)
    {
    case nf::ipc::IpcCmd::ClientHello:
    case nf::ipc::IpcCmd::SyncRequest:
    case nf::ipc::IpcCmd::RuntimeReady:
    {
        std::unique_ptr<IpcdEvent> event = m_eventFactory->create(std::move(msg));
        m_serviceManager->postEvent(std::move(event));
        break;
    }

    default:
        LOG_DEBUG("IpcdRxRouter: bypass routing cmd={}", nf::ipc::IpcProtocol::cmdToStr(cmd));
        m_txRouter->handleIpcMessage(std::move(msg));
        break;
    }
}

} // namespace nf::ipcd
