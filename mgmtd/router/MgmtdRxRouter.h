#pragma once

#include "router/RxRouter.h"
#include "event/MgmtdEvent.h"
#include "event/MgmtdEventFactory.h"
#include "service/MgmtdServiceManager.h"

#include "http/HttpMessage.h"
#include "http/HttpResponder.h"

#include <memory>

namespace pz::mgmtd
{

// The daemon's single inbound router. It turns messages from BOTH transports into events
// for the service layer, symmetric per transport:
//   - IPC:  handleIpcMessage()  — wrap IpcMessage -> event, post (async; drained in tick).
//   - HTTP: dispatchHttp()       — wrap request -> WebEvent, post, drain synchronously,
//                                  return the filled response.
// It carries no routing/auth/business logic — that lives in the services (handleEvent).
class MgmtdRxRouter : public pz::router::RxRouter
{
public:
    MgmtdRxRouter(MgmtdEventFactory* eventFactory,
                  MgmtdServiceManager* serviceManager);
    ~MgmtdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    // HTTP ingress: post a WebEvent carrying the request and the responder, then return.
    // The response is filled by the service layer and delivered via the responder during
    // the action drain — no synchronous wait, symmetric with IPC ingress.
    void dispatchHttp(pz::http::HttpRequest req,
                      std::shared_ptr<pz::http::HttpResponder> responder);

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdServiceManager* m_serviceManager{nullptr};
};

} // namespace pz::mgmtd
