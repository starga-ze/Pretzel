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
        LOG_ERROR("service manager is not initialized");
        return;
    }

    if (!msg)
    {
        LOG_WARN("received null IPC message — skipping");
        return;
    }

    LOG_TRACE("recv (cmd={}, src={})",
              pz::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    if (msg->getCmd() == pz::ipc::IpcCmd::ConfigReloadResponse)
    {
        LOG_INFO("config reload acknowledged by engined");
        m_serviceManager->completeReload();
        return;
    }

    if (msg->getCmd() == pz::ipc::IpcCmd::CommitQueueStatus)
    {
        const auto& pl = msg->getPayload();
        if (!pl.empty())
        {
            m_serviceManager->setCommitQueue(std::string(pl.begin(), pl.end()));
            LOG_DEBUG("commit queue snapshot updated");
        }
        return;
    }

    // SSO (SAML) verification result from authd — keyed by the request ticket (seqNo);
    // the browser poll (/api/auth/saml/result) consumes it.
    if (msg->getCmd() == pz::ipc::IpcCmd::AuthSamlAcsResponse)
    {
        const auto& pl = msg->getPayload();
        m_serviceManager->setSsoResult(msg->getSeqNo(),
                                       std::string(pl.begin(), pl.end()));
        return;
    }

    std::unique_ptr<MgmtdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

} // namespace pz::mgmtd
