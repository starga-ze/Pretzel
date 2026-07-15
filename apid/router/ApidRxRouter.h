#pragma once

#include "router/RxRouter.h"
#include "event/ApidEvent.h"
#include "event/ApidEventFactory.h"
#include "service/ApidServiceManager.h"

#include "http/HttpMessage.h"

#include <memory>

namespace pz::apid
{

// The daemon's single inbound router. It turns messages from BOTH transports into events
// for the service layer, symmetric per transport:
//   - IPC:  handleIpcMessage()  — wrap IpcMessage -> event, post (async; drained in tick).
//   - HTTP: dispatchHttp()       — wrap request -> IngestEvent, post, then return (async).
// It carries no routing/auth/business logic — that lives in the services (handleEvent).
class ApidRxRouter : public pz::router::RxRouter
{
public:
    ApidRxRouter(ApidEventFactory* eventFactory,
                 ApidServiceManager* serviceManager);
    ~ApidRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    // HTTP ingress: post an IngestEvent carrying the request and its SessionId, then return.
    // The service fills a response and posts an IngestAction; egress resolves the
    // SessionId back to the connection during the action drain — symmetric with IPC ingress.
    void handleHttpMessage(pz::http::HttpRequest req, pz::http::SessionId id) override;

private:
    ApidEventFactory* m_eventFactory{nullptr};
    ApidServiceManager* m_serviceManager{nullptr};
};

} // namespace pz::apid
