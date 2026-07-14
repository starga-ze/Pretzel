#pragma once

#include "http/HttpMessage.h"

#include <string>

namespace pz::apid
{

class ApidServiceManager;
class IngestEvent;

// Ingest domain logic. It handles inbound HTTP events (dispatched through the
// ServiceManager queue, same as IPC events): it routes by method/target, authenticates
// the shared token, validates the egress observation, and (future) emits an EgressReport
// IPC action to engined (the single DB writer). It fills the event's response slot.
// Transport-agnostic — it never sees boost::beast. Runs on the single poll thread.
class IngestService
{
public:
    IngestService();

    // Routes the request, fills a response, and posts an IngestResponseAction that delivers it.
    void handleEvent(ApidServiceManager& serviceManager, const IngestEvent& event);

private:
    // Pure routing: fill resp from req (no transport, no queue). Split out so handleEvent
    // can post the delivery action after it regardless of which branch produced the response.
    void route(const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // Extracts the "Bearer <token>" value from a raw Authorization header ("" if absent).
    std::string bearerToken(const std::string& authorization) const;

    // Shared secret; injected via PZ_APID_TOKEN. Empty => reject all (fail-closed).
    std::string m_ingestToken;
};

} // namespace pz::apid
