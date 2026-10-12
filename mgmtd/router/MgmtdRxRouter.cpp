#include "router/MgmtdRxRouter.h"


#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <memory>

namespace pz::mgmtd
{

MgmtdRxRouter::MgmtdRxRouter(MgmtdEventFactory* eventFactory, MgmtdServiceManager* serviceManager)
    : m_eventFactory(eventFactory), m_serviceManager(serviceManager)
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

    LOG_TRACE("recv (cmd={}, src={})", pz::ipc::IpcProtocol::cmdToStr(msg->getCmd()),
              pz::ipc::IpcProtocol::daemonToStr(msg->getSrc()));

    std::unique_ptr<MgmtdEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void MgmtdRxRouter::handleHttpMessage(pz::http::HttpRequest req, pz::http::SessionId id)
{
    m_serviceManager->postEvent(m_eventFactory->create(std::move(req), id));
}

void MgmtdRxRouter::handleGrpcMessage(GrpcCmd cmd, std::uint32_t ticket, std::string json)
{
    LOG_TRACE("recv (rpc={}, ticket={})", grpcCmdToStr(cmd), ticket);

    m_serviceManager->postEvent(m_eventFactory->create(cmd, ticket, std::move(json)));
}

}
