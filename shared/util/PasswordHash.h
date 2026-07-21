#pragma once

#include <string>

namespace pz::util
{

// Local-account password storage.
//
// A stored password is PBKDF2-HMAC-SHA256 over the password and the account's salt, written as
// "pbkdf2$<iterations>$<hex>". The algorithm and cost travel inside the value, so a hash can
// always be verified by the code that reads it — including hashes written before the cost was
// raised — and the salt keeps its own column, so the schema is unchanged.
//
// Earlier builds stored a bare SHA-256 hex digest of password+salt. That is one pass of a fast
// hash: a leaked local_users table is brute-forced at GPU speed, which is why this exists.
// verifyPassword() still accepts those values so existing logins survive the upgrade, and
// needsRehash() tells the caller to re-store the credential in the current format after a
// successful login.
//
// COST: verification is deliberately slow — measured at ~170 ms per call at the setting below.
// The daemons run one cooperative loop, so a verify blocks it for that long. That is fine for a
// login (rare, operator-initiated) but it means callers MUST rate-limit failed attempts:
// without a throttle an unauthenticated caller stalls the daemon just by guessing repeatedly.
// See pz::mgmtd::AuthService::noteLoginFailure.
//
// Raising the cost is safe for existing accounts — a stored value is always verified at the
// cost recorded inside it — but re-measure before doing so; at 600k this becomes ~490 ms.

// Current cost. Exposed so callers can report it and tests can assert it moved.
constexpr int kPbkdf2Iterations = 210000;

// Hashes `password` with `salt` in the current format. Returns empty on failure.
std::string hashPassword(const std::string& password, const std::string& salt);

// Constant-time comparison against `stored`, in either the current or the legacy format.
// False for an empty or malformed `stored`.
bool verifyPassword(const std::string& password, const std::string& salt, const std::string& stored);

// True when `stored` is legacy or below the current cost. Call after a successful verify: the
// plaintext is in hand exactly then, which is the only moment the value can be upgraded.
bool needsRehash(const std::string& stored);

// 16 bytes from the CSPRNG, hex-encoded. Not a secret, but it must be unpredictable and unique
// per account so one rainbow table cannot cover two of them.
std::string generateSalt();

}
