#pragma once

#include "http/HttpMessage.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace pz::router { class RxRouter; }

namespace pz::http
{

class HttpSessionBase;

// The daemon-neutral HTTP transport handler, the analogue of pz::ipc::IpcClientHandler:
// it owns the codec-facing plumbing and forwards both directions to/from the injected
// RxRouter. Because an HTTP server has many concurrent connections (unlike the IPC client's
// single socket), it also owns a registry of live sessions keyed by SessionId — the same
// role IpcServer's fd->connection map plays — so egress can address the right one.
//
//   ingress(request, id) -> RxRouter::dispatchHttp   [session read -> service event]
//   egress(response, id) -> session->send()          [TxRouter -> wire], id resolved here
//
// A session registers itself on accept (addSession assigns its SessionId and keeps it alive
// while parked) and drops out on close (removeSession). Single poll thread — no locking.
class HttpHandler
{
public:
    HttpHandler() = default;

    void setRxRouter(pz::router::RxRouter* rxRouter);

    // Registry: sessions self-register on accept and self-remove on close. addSession stamps
    // the session's id and returns it; the registry's shared_ptr keeps a parked session alive
    // (the request/response gap has no pending async op of its own).
    SessionId addSession(std::shared_ptr<HttpSessionBase> session);
    void      removeSession(SessionId id);

    // Ingress: forward a parsed request to the router, tagged with its connection.
    void ingress(HttpRequest request, SessionId id);

    // Egress: resolve the connection and write the response; a no-op (logged) if the session
    // has already closed — the HTTP analogue of IPC egress to a departed fd.
    void egress(HttpResponse response, SessionId id);

private:
    pz::router::RxRouter* m_rxRouter{nullptr};

    std::unordered_map<SessionId, std::shared_ptr<HttpSessionBase>> m_sessions;
    SessionId m_nextId{0};
};

} // namespace pz::http
