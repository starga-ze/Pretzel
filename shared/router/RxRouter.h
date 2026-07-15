#pragma once

#include "ipc/IpcMessage.h"
#include "http/HttpMessage.h"

#include <memory>

namespace pz::router
{

// Inbound router base, shared by every daemon. IPC ingress is mandatory; HTTP ingress is
// optional (only daemons with an HttpServer override dispatchHttp) so it defaults to a no-op
// rather than being pure — mirrors how not every daemon speaks HTTP.
class RxRouter
{
    public:
        RxRouter() = default;
        virtual ~RxRouter() = default;

        virtual void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage>) = 0;

        // HTTP ingress: a request tagged with the connection it arrived on (SessionId).
        virtual void handleHttpMessage(pz::http::HttpRequest, pz::http::SessionId) {};

};

}
