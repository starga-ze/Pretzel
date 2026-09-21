#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace pz::collectord
{

class CollectordServiceManager;

// Fetches one AI vendor's model catalog and hands the refined list to engined.
//
// The console used to ship this list inside its JavaScript, so a model released after a build could
// not be selected until the next one. It comes from the vendor now, and it comes through here for
// the reason every other outbound vendor call does: collectord owns them all, and it is the only
// daemon with an io_context to run one on.
//
// Two hops, not one, and the first is what makes the second possible. The vendor's list endpoint
// authenticates with the same key the turns use, and that key is sealed in engined's store — so the
// operation asks engined for the sealed blob, opens it here with credentials.key, and only then
// calls the vendor. The plaintext never crosses the socket, which is the same arrangement the API
// credentials have (ApiCredentialState{Request,Response}).
//
// Asked per refresh rather than cached. The other credential path caches because periodic collection
// would otherwise hit the database on every poll; this one runs when an operator presses a button,
// and a cache would only introduce the case where a key entered a moment ago is not the key used.
class AiModelController
{
public:
    // Step 1, on AiModelUpdateRequest: remember the ticket and ask engined for this vendor's sealed
    // key. `input` is { provider }. Answers the ticket immediately on a bad request — mgmtd is
    // holding a browser on it, so no path here may leave it pending.
    void refresh(CollectordServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& input);

    // Step 2, on AiCredentialStateResponse: open the blob and call the vendor. `input` is
    // { id, key_enc }. A response whose seqNo names no pending refresh is dropped — engined answers
    // the asker, so one that matches nothing is a reply to a refresh that already gave up.
    void receiveKey(CollectordServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& input);

private:
    // The refreshes waiting on a key, by the ticket mgmtd is holding. Small and short-lived: one
    // entry per press of Update, removed when the vendor answers or the attempt is abandoned.
    std::unordered_map<std::uint32_t, std::string> m_pending;   // seqNo -> provider
};

}
