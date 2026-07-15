#pragma once

#include "http/HttpMessage.h"

namespace pz::http
{

// Transport-internal base for a live HTTP connection (HttpSession / HttpsSession). It exists
// only so the HttpHandler can keep a heterogeneous registry of parked connections keyed by
// SessionId and write a response back to any of them without knowing plaintext-vs-TLS.
//
// It is NOT seen by the service/action layer — that layer only ever holds a SessionId (the
// beast-free handle), the HTTP analogue of an IPC daemon id. This is the deliberate contrast
// with the old HttpResponder, which was threaded through every event/action as a shared_ptr.
class HttpSessionBase
{
public:
    virtual ~HttpSessionBase() = default;

    // Encode + async-write the response on this connection. Beast-free signature; the
    // concrete session owns the beast translation (it needs the originating request).
    virtual void send(HttpResponse response) = 0;

    SessionId id() const { return m_id; }
    void      setId(SessionId id) { m_id = id; }

protected:
    SessionId m_id{0};
};

} // namespace pz::http
