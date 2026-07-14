#pragma once

#include "http/HttpMessage.h"
#include "http/HttpResponder.h"

#include <memory>

namespace pz::http
{

// Transport-facing contract for the HTTP server, mirroring pz::ipc's ingress path. The
// listener/session layer depends only on this base — never on a concrete router — so each
// daemon (mgmtd, apid, ...) plugs in its own handler that forwards a request into the
// event/action machinery.
//
// It is beast-free and asynchronous: the session translates the parsed request into a
// transport-agnostic HttpRequest and passes it with a responder. handle() forwards
// both to the daemon router (which posts an HttpEvent) and returns immediately — it must
// NOT block. The response is produced later by the service layer and delivered through
// responder->send(), exactly as IPC egress is a queued action. Flows that must await a
// cross-daemon reply are split across requests (the client polls a result endpoint) — see
// mgmtd's SAML handlers.
class HttpHandler
{
public:
    HttpHandler() = default;
    virtual ~HttpHandler() = default;

    virtual void handle(HttpRequest request,
                        std::shared_ptr<HttpResponder> responder) = 0;
};

} // namespace pz::http
