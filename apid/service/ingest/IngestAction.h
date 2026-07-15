#pragma once

#include "action/ApidAction.h"

#include "http/HttpMessage.h"

namespace pz::apid
{

// The Ingest domain's egress action (peer of BootstrapAction). IngestService posts it after
// filling a response, carrying the response plus the SessionId of the originating connection.
// Like every action, dispatch() just double-dispatches into the owning service
// (IngestService::handleAction) — the service performs the egress via the TxRouter, uniform
// with IPC (BootstrapAction).
class IngestAction final : public ApidAction
{
public:
    IngestAction(pz::http::HttpResponse response, pz::http::SessionId id);

    void dispatch(ApidServiceManager& serviceManager) override;

    // Accessors for IngestService::handleAction. response() is mutable so the service can move
    // the body out on egress rather than copy it.
    pz::http::HttpResponse& response();
    pz::http::SessionId     sessionId() const;

private:
    pz::http::HttpResponse m_response;
    pz::http::SessionId    m_sessionId;
};

} // namespace pz::apid
