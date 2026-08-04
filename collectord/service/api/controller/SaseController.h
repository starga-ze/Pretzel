#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>

namespace pz::collectord
{

class ApiService;
class CollectordServiceManager;

// The SASE half of the endpoint test: calls one published endpoint on a Palo Alto cloud product with
// an OAuth bearer token minted for the tenant, and reports what came back. Paired with
// NgfwController — see the note there for why the two are separate rather than one branching class.
//
// What makes a SASE call different, end to end:
//
//   who     the "device" is a TENANT, not an address. Its `target` is the tsg_id, which is the
//           token's scope; it is not somewhere you connect to. The host being called comes from the
//           endpoint instead, so one tenant can be read through several product APIs.
//   trust   a public CA endpoint, so the chain and hostname are verified. There is no fingerprint
//           to pin and no trust-on-first-use step — the whole pinning dance NGFW needs is absent.
//   auth    Authorization: Bearer, from a short-lived token minted with the tenant's client
//           id/secret. Reuses the token already issued for the credential when there is one, and
//           mints a transient one otherwise — the same shape as NGFW's transient keygen.
//   headers the operator supplies them. ZTNA requires x-panw-region, which cannot be derived from
//           the tenant and answers 424 "tenant not found" rather than an error when it is wrong,
//           so it has to be an editable part of the endpoint.
//
// Product dispatch lives here (SaseProduct): today only ZTNA is served. When a second product lands,
// what differs is the default host, the required headers and the pagination shape — all of which
// this controller already treats as endpoint data rather than as constants, so the second product
// is a defaults entry plus its own response handling, not another controller.
class SaseController
{
public:
    void runEndpointTest(ApiService& api, CollectordServiceManager& sm, std::uint32_t seqNo,
                         const nlohmann::json& input);
};

}
