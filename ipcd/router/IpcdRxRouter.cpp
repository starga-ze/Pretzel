#include "router/IpcdRxRouter.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::ipcd
{

IpcdRxRouter::IpcdRxRouter(IpcdEventFactory* eventFactory, IpcdServiceManager* serviceManager, IpcdTxRouter* txRouter)
    : m_eventFactory(eventFactory), m_serviceManager(serviceManager), m_txRouter(txRouter)
{
}

void IpcdRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!m_serviceManager || !m_txRouter)
    {
        LOG_ERROR("dependencies not ready (serviceManager={}, txRouter={})", static_cast<bool>(m_serviceManager),
                  static_cast<bool>(m_txRouter));
        return;
    }

    if (!msg)
    {
        LOG_WARN("received null IPC message — skipping");
        return;
    }

    LOG_TRACE("recv (cmd={}, src={})", pz::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    const pz::ipc::IpcCmd cmd = msg->getCmd();

    switch (cmd)
    {
    case pz::ipc::IpcCmd::ClientHello:
    case pz::ipc::IpcCmd::SyncRequest:
    case pz::ipc::IpcCmd::RuntimeReady:
    {
        std::unique_ptr<IpcdEvent> event = m_eventFactory->create(std::move(msg));
        m_serviceManager->postEvent(std::move(event));
        break;
    }

    // Everything else is somebody else's mail, and forwarding it is this daemon's whole job. The
    // deliberate exception to the rule the other seven daemons keep: an inbound message does NOT
    // become an event here, because a relay that put every message through its own service loop
    // would add a queue round trip to every IPC in the appliance. Nothing is read out of the
    // message to decide this — only that no case above claimed it — so no domain logic can settle
    // in the branch.
    default:
        LOG_TRACE("bypass routing (cmd={})", pz::ipc::IpcProtocol::cmdToStr(cmd));
        m_txRouter->handleIpcMessage(std::move(msg));
        break;
    }
}

}
