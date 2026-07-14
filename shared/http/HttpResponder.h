#pragma once

#include "http/HttpMessage.h"

namespace pz::http
{

// Beast-free sink for a response to a parked request. The transport (HttpSession /
// HttpsSession) implements it: send() converts the DTO to a beast response and async-writes
// it on the originating connection. It is handed to the service layer as a shared_ptr so
// the connection stays alive from ingress until the response is written — mirroring how IPC
// egress is a queued action rather than an inline return.
//
// The HTTP request/response cycle is therefore fully asynchronous and symmetric with IPC:
//   ingress  → post HttpEvent (carries the responder)      [like an inbound IpcMessage event]
//   service  → postAction(HttpResponseAction{responder,…}) [like an outbound IPC action]
//   drain    → responder->send(response)                    [like the action's IPC egress]
class HttpResponder
{
public:
    virtual ~HttpResponder() = default;

    // Deliver the response. Called on the single poll thread during the action drain; it
    // schedules an async write that the next io_context poll flushes.
    virtual void send(HttpResponse response) = 0;
};

} // namespace pz::http
