#pragma once

#include <optional>
#include <string>

namespace pz::util
{

// Encryption for credentials that must be handed back out, not merely verified.
//
// A password is stored as a one-way hash because nothing ever needs the original again. A device
// API key is the opposite: pretzel has to present it to the firewall on every call, so it needs
// the plaintext back. That means reversible encryption, which means a key — /etc/pretzel/
// credentials.key, generated per install by script/start.py and readable only by root.
//
// What this buys: a copy of the database alone is useless. Dumps, backups, replicas and a stolen
// database login all yield ciphertext. What it does not buy: protection from someone who already
// has root on the appliance, since the key sits on the same disk. Closing that gap needs an HSM
// or an external KMS.
//
// Ciphertext is AES-256-GCM, serialised as base64(nonce ‖ tag ‖ ciphertext) so it stores in a
// TEXT column without the escaping a BYTEA round-trip would need. GCM authenticates as well as
// encrypts, so tampering is detected rather than decrypting to garbage.
namespace secret
{

// Path of the master key. Overridable for tests; defaults to /etc/pretzel/credentials.key
// (PRETZEL_CONFIG_DIR moves it alongside the other deployed config).
std::string keyPath();

// True once a usable 32-byte key has been read. Callers should check before offering to store
// anything, so a missing key surfaces as "not configured" instead of a failure at write time.
bool available();

// std::nullopt when the key is missing or the input cannot be authenticated — a wrong key,
// truncated storage or a modified row all land here rather than returning rubbish.
std::optional<std::string> encrypt(const std::string& plaintext);
std::optional<std::string> decrypt(const std::string& encoded);

}

}
