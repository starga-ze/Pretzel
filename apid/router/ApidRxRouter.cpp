#include "router/ApidRxRouter.h"

#include "service/ingest/IngestEvent.h"

#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <memory>

namespace pz::apid
{

ApidRxRouter::ApidRxRouter(ApidEventFactory* eventFactory,
                           ApidServiceManager* serviceManager)
    : m_eventFactory(eventFactory),
      m_serviceManager(serviceManager)
{
}

void ApidRxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
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

    std::unique_ptr<ApidEvent> event = m_eventFactory->create(std::move(msg));

    m_serviceManager->postEvent(std::move(event));
}

void ApidRxRouter::dispatchHttp(pz::http::HttpRequest req,
                                std::shared_ptr<pz::http::HttpResponder> responder)
{
    // Post and return. The event is drained by the ServiceManager on this same tick (its
    // execute() runs right after the HTTP poll that produced this call); the service fills a
    // response and posts an IngestResponseAction that calls responder->send().
    m_serviceManager->postEvent(
        std::make_unique<IngestEvent>(std::move(req), std::move(responder)));
}

} // namespace pz::apid
