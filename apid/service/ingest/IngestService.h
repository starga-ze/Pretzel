#pragma once

#include <string>

namespace pz::apid
{

class ApidServiceManager;
class HttpEvent;

// Ingest domain logic. It handles inbound HTTP events (dispatched through the
// ServiceManager queue, same as IPC events): it routes by method/target, authenticates
// the shared token, validates the egress observation, and (future) emits an EgressReport
// IPC action to engined (the single DB writer). It fills the event's response slot.
// Transport-agnostic — it never sees boost::beast. Runs on the single poll thread.
class IngestService
{
public:
    IngestService();

    void handleEvent(ApidServiceManager& serviceManager, const HttpEvent& event);

private:
    // Extracts the "Bearer <token>" value from a raw Authorization header ("" if absent).
    std::string bearerToken(const std::string& authorization) const;

    // Shared secret; injected via PZ_APID_TOKEN. Empty => reject all (fail-closed).
    std::string m_ingestToken;
};

} // namespace pz::apid
