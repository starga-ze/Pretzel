#pragma once

#include "event/ApidEvent.h"

#include "http/HttpMessage.h"

namespace pz::apid
{

// An inbound HTTP request carried through the ServiceManager event queue exactly like an
// IPC-derived event. It holds the transport-agnostic request and the SessionId of the
// connection it arrived on. The service produces a response and posts an IngestAction carrying
// that same SessionId — so the whole cycle is async and symmetric with IPC.
class IngestEvent final : public ApidEvent
{
public:
    IngestEvent(pz::http::HttpRequest request, pz::http::SessionId id);

    void dispatch(ApidServiceManager& serviceManager) override;

    const pz::http::HttpRequest& request() const;
    pz::http::SessionId          sessionId() const;

private:
    pz::http::HttpRequest m_request;
    pz::http::SessionId   m_sessionId;
};

} // namespace pz::apid
