#pragma once

#include "service/apikey/ApiKeyEvent.h"

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
// mgmtd runs the key generation (it is the one talking to the device) but engined is the sole
// database writer, so mgmtd encrypts the secret and sends it here, exactly as it does for an
// admin password change.
class ApiKeyService
{
public:
    ApiKeyService() = default;
    ~ApiKeyService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const ApiKeyEvent& event);

private:
    void storeState(const std::string& payloadJson);
};

}
