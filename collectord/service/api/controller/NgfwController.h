#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>

namespace pz::collectord
{

class ApiService;
class CollectordServiceManager;

// The NGFW half of the endpoint test: calls one published endpoint on the operator's own firewall
// with a PAN-OS key and reports what came back. Separated from credential issuance so a wrong path
// is distinguishable from a wrong credential; if the profile has no issued key yet, it issues a
// transient one first (not persisted — the point here is the path, not storing a credential) and
// then calls.
//
// Paired with SaseController. ApiService picks between them on the endpoint's device type, because
// the two share nothing below that line: a pinned self-signed TLS exchange against a customer's box
// with the key in a query parameter or an X-PAN-KEY header, versus a CA-verified call to Palo Alto's
// cloud with a minted bearer token. One controller trying to be both would be a switch statement
// wearing a class.
class NgfwController
{
public:
    void runEndpointTest(ApiService& api, CollectordServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& input);
};

}
