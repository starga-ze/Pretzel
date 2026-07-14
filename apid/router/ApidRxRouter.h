#pragma once

#include "router/RxRouter.h"
#include "event/ApidEvent.h"
#include "event/ApidEventFactory.h"
#include "service/ApidServiceManager.h"

#include "http/HttpMessage.h"
#include "http/HttpResponder.h"

#include <memory>

namespace pz::apid
{

// The daemon's single inbound router. It turns messages from BOTH transports into events
// for the service layer, symmetric per transport:
//   - IPC:  handleIpcMessage()  — wrap IpcMessage -> event, post (async; drained in tick).
//   - HTTP: dispatchHttp()       — wrap request -> IngestEvent, post, drain synchronously,
//                                  return the filled response.
// It carries no routing/auth/business logic — that lives in the services (handleEvent).
class ApidRxRouter : public pz::router::RxRouter
{
public:
    ApidRxRouter(ApidEventFactory* eventFactory,
                 ApidServiceManager* serviceManager);
    ~ApidRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    // HTTP ingress: post an IngestEvent carrying the request and the responder, then return.
    // The response is filled by the service layer and delivered via the responder during
    // the action drain — no synchronous wait, symmetric with IPC ingress.
    void dispatchHttp(pz::http::HttpRequest req,
                      std::shared_ptr<pz::http::HttpResponder> responder);

private:
    ApidEventFactory* m_eventFactory{nullptr};
    ApidServiceManager* m_serviceManager{nullptr};
};

} // namespace pz::apid
