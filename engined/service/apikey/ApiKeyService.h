#pragma once

#include "service/apikey/ApiKeyEvent.h"

#include <cstdint>
#include <string>

namespace pz::engined
{

class EnginedServiceManager;

// Persists what pretzel learns about a device API key: the issued secret, when it expires, and
// how the last verification went.
//
// None of it belongs in running_config. That document is append-versioned, diffed before publish
// and exported by Save-to-file, so a key written there would be permanent, visible to whoever
// reviews the change, and carried into every backup — while re-issuing one would mint a
// configuration version for something no operator authored. It lands in api_key_state instead,
// the same reasoning that keeps admin passwords in local_users.
//
// scand runs the key generation (it is the one talking to the device) but engined is the sole
// database writer, so scand encrypts the secret and sends it here, exactly as mgmtd does for an
// admin password change.
//
// The way back out is sendState(): scand asks for the issued keys and gets them back STILL
// SEALED, then opens them with credentials.key. engined never holds a plaintext key and the
// socket never carries one — the same contract as the inbound direction.
class ApiKeyService
{
public:
    ApiKeyService() = default;
    ~ApiKeyService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const ApiKeyEvent& event);

private:
    void storeState(const std::string& payloadJson);

    // Answers ApiKeyStateRequest with every issued key, sealed. seqNo is the requester's
    // correlation value and is echoed back.
    void sendState(EnginedServiceManager& serviceManager, std::uint32_t seqNo);
};

}
