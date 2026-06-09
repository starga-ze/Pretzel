#include "router/EnginedRxRouter.h"

#include "config/Config.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

namespace pz::engined
{

EnginedRxRouter::EnginedRxRouter(EnginedEventFactory* eventFactory,
                                  EnginedServiceManager* serviceManager)
    : m_eventFactory(eventFactory),
      m_serviceManager(serviceManager)
{
}

void EnginedRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!m_serviceManager)
    {
        LOG_ERROR("Engined RxRouter: service manager is not initialized");
        return;
    }

    if (!msg)
    {
        LOG_WARN("Engined RxRouter: received null IPC message — skipping");
        return;
    }

    LOG_DEBUG("recv cmd={} src={}",
              pz::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    if (msg->getCmd() == pz::ipc::IpcCmd::ConfigReloadRequest)
    {
        LOG_INFO("ConfigReloadRequest from mgmtd — fanning out to service layer");

        static constexpr pz::ipc::IpcDaemon kServiceDaemons[] = {
            pz::ipc::IpcDaemon::Authd,
            pz::ipc::IpcDaemon::Icmpd,
            pz::ipc::IpcDaemon::Snmpd,
            pz::ipc::IpcDaemon::Topologyd,
        };

        for (const auto dst : kServiceDaemons)
        {
            auto cfgMsg = std::make_unique<pz::ipc::IpcMessage>();
            cfgMsg->setSrc(pz::ipc::IpcDaemon::Engined);
            cfgMsg->setDst(dst);
            cfgMsg->setCmd(pz::ipc::IpcCmd::ConfigReload);
            cfgMsg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
            m_serviceManager->txRouter().handleIpcMessage(std::move(cfgMsg));
            LOG_INFO("ConfigReload sent to {}", pz::ipc::IpcProtocol::daemonToStr(dst));
        }

        // Engined does not restart itself: it is the orchestrator and must
        // remain connected to ipcd to send RuntimeStart once the service layer
        // has cycled. Reload its own config in-place and re-enter WaitSync.
        pz::config::Config::invalidateConfigCache();
        m_serviceManager->bootstrapService().scheduleServiceReload();
        return;
    }

    std::unique_ptr<EnginedEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

} // namespace pz::engined
