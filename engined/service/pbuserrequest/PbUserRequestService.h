#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace pz::engined
{

class EnginedServiceManager;

// Prisma Browser user requests, projected out of the collected list document into pb_user_request.
//
// This is the one place in the appliance that knows what a Prisma Browser list body looks like, and
// that is the point. collectord fetches bytes and keeps every body opaque; mgmtd serves rows; the
// vendor's document shape lives here, with the service that owns the table it fills.
//
// CollectionService hands over every sample it stores, and this decides whether the endpoint it
// came from is a Prisma Browser user-request endpoint, ignoring it if not — so nothing has to be
// declared for this to start working. A list endpoint collected on a schedule IS the trigger.
//
// There was a second half: one GET /user-requests/{id} per row per cycle, stored beside the list
// document so the console could show whether the single-record read carried anything extra.
// Measured on 2026-10-01 against the same request in both Pending and Approved states, it carried
// nothing — the two were identical field for field, including the four that only exist once an
// admin has responded. It was removed rather than left spending a vendor call per row per minute
// to re-fetch what the list had already said.
class PbUserRequestService
{
public:
    // Called for every stored collection sample. Cheap and silent for the ones that are not ours:
    // the endpoint's subtype is checked before the body is parsed.
    void onCollectionSample(const std::string& connectorOid, const std::string& endpointOid,
                            const std::string& body);
};

}
