#pragma once

#include "action/MgmtdAction.h"

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

// The Web domain's egress action (peer of BootstrapAction/HeartbeatAction). WebService posts
// it after filling a response, carrying the response plus the SessionId of the originating
// connection. Like every action, dispatch() just double-dispatches into the owning service
// (WebService::handleAction) — the service performs the egress via the TxRouter. This keeps
// the "event produces an action; the service (from the action drain) performs egress" shape
// uniform with IPC (BootstrapAction/HeartbeatAction).
class WebAction final : public MgmtdAction
{
public:
    WebAction(pz::http::HttpResponse response, pz::http::SessionId id);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    // Accessors for WebService::handleAction. response() is mutable so the service can move
    // the (potentially large, e.g. static-asset) body out on egress rather than copy it.
    pz::http::HttpResponse& response();
    pz::http::SessionId     sessionId() const;

private:
    pz::http::HttpResponse m_response;
    pz::http::SessionId    m_sessionId;
};

} // namespace pz::mgmtd
