#pragma once

#include "router/RxRouter.h"
#include "event/MgmtdEvent.h"
#include "event/MgmtdEventFactory.h"
#include "service/MgmtdServiceManager.h"

#include "http/HttpMessage.h"

#include <memory>

namespace pz::mgmtd
{

// The daemon's single inbound router. It turns messages from BOTH transports into events
// for the service layer, symmetric per transport:
//   - IPC:  handleIpcMessage()  — wrap IpcMessage -> event, post (async; drained in tick).
//   - HTTP: dispatchHttp()       — wrap request -> WebEvent, post, then return (async).
// It carries no routing/auth/business logic — that lives in the services (handleEvent).
class MgmtdRxRouter : public pz::router::RxRouter
{
public:
    MgmtdRxRouter(MgmtdEventFactory* eventFactory,
                  MgmtdServiceManager* serviceManager);
    ~MgmtdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    // HTTP ingress: post a WebEvent carrying the request and its SessionId, then return. The
    // service fills a response and posts a WebAction; egress resolves the SessionId
    // back to the connection during the action drain — symmetric with IPC ingress.
    void handleHttpMessage(pz::http::HttpRequest req, pz::http::SessionId id) override;

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdServiceManager* m_serviceManager{nullptr};
};

} // namespace pz::mgmtd
