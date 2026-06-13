#pragma once

#include <string>

namespace pz::util
{

// SHA-256 of (password + salt), returned as a lowercase hex string. Shared by the
// single DB writer (engined: default-admin seed) and mgmtd (login/verify + computing
// the hash for a password change) so both sides agree on the exact algorithm.
std::string hashSha256(const std::string& password, const std::string& salt);

// Random hex salt for a fresh credential.
std::string generateSalt();

} // namespace pz::util
