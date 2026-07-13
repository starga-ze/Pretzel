#pragma once

#include "router/RxRouter.h"
#include "event/ApidEvent.h"
#include "event/ApidEventFactory.h"
#include "service/ApidServiceManager.h"

#include "http/HttpExchange.h"

namespace pz::apid
{

// The daemon's single inbound router. It turns messages from BOTH transports into events
// for the service layer, symmetric per transport:
//   - IPC:  handleIpcMessage()  — wrap IpcMessage -> event, post (async; drained in tick).
//   - HTTP: dispatchHttp()       — wrap request -> HttpEvent, post, drain synchronously,
//                                  return the filled response.
// It carries no routing/auth/business logic — that lives in the services (handleEvent).
class ApidRxRouter : public pz::router::RxRouter
{
public:
    ApidRxRouter(ApidEventFactory* eventFactory,
                 ApidServiceManager* serviceManager);
    ~ApidRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    pz::http::HttpResponse dispatchHttp(const pz::http::HttpRequestView& req);

private:
    ApidEventFactory* m_eventFactory{nullptr};
    ApidServiceManager* m_serviceManager{nullptr};
};

} // namespace pz::apid
