#pragma once

#include "event/MgmtdEvent.h"

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

// An inbound HTTP request carried through the ServiceManager event queue exactly like an
// IPC-derived event. It holds the transport-agnostic request and the SessionId of the
// connection it arrived on. WebService produces a response and posts a WebAction carrying
// that same SessionId — so the whole cycle is async and symmetric with IPC.
class WebEvent final : public MgmtdEvent
{
public:
    WebEvent(pz::http::HttpRequest request, pz::http::SessionId id);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    const pz::http::HttpRequest& request() const;
    pz::http::SessionId          sessionId() const;

private:
    pz::http::HttpRequest m_request;
    pz::http::SessionId   m_sessionId;
};

} // namespace pz::mgmtd
