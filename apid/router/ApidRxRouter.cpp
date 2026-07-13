#include "router/ApidRxRouter.h"

#include "service/ingest/HttpEvent.h"

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

pz::http::HttpResponse ApidRxRouter::dispatchHttp(const pz::http::HttpRequestView& req)
{
    // Shared slot the service fills; kept alive here while the event (which holds a copy)
    // is processed. Seeded with the default 404 for unmatched routes.
    auto response = std::make_shared<pz::http::HttpResponse>();

    m_serviceManager->postEvent(std::make_unique<HttpEvent>(req, response));

    // ServiceManager::execute() drains the queue synchronously within this call, so the
    // response is filled by the time it returns.
    m_serviceManager->execute();

    return *response;
}

} // namespace pz::apid
