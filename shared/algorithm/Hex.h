#pragma once

#include <cstddef>
#include <string>

namespace pz::algorithm
{

// Hex rendering and random token minting, in one place because the two used to travel together:
// every daemon that needed a token wrote its own "pick nibbles from a table" loop, and each one
// chose its own source of randomness on the way past.
//
// That choice is why this is shared rather than copied. mgmtd learned the hard way that a session
// id minted from a seeded Mersenne Twister is enumerable — see AuthService::generateSessionId —
// and fixed it there, but the lesson never reached the three other places that mint bearer-grade
// tokens. Here the secure source is the only source, so the next caller inherits the fix instead
// of repeating the mistake.

// Lower-case by default, because every hex value pretzel stores or logs (password digests, session
// ids, transaction ids) is lower-case. TLS certificate fingerprints are the exception and ask for
// upper.
std::string toHex(const void* data, std::size_t len, bool upper = false);

// `nBytes` of cryptographic randomness, rendered as 2*nBytes lower-case hex characters.
//
// Returns an EMPTY string when the system's entropy source fails. That is not a theoretical branch
// to ignore: the value is normally a bearer token — an OIDC state, a PKCE verifier, a session id —
// and a caller that shipped an empty one would be issuing a credential everybody can guess. Check
// it and fail the operation.
std::string randomHex(std::size_t nBytes);

}
