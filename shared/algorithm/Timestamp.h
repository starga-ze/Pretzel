#pragma once

#include <chrono>
#include <ctime>
#include <string>

namespace pz::algorithm
{

// ISO-8601 UTC, "2026-09-15T04:05:06Z".
//
// Three places were formatting this by hand with the same strftime pattern — a SAML AuthnRequest's
// IssueInstant, an SSO ticket's timestamp, and a SASE token's expiry. The format is not decoration:
// the trailing Z is what tells an IdP the value is UTC rather than local time, and a clock-skew
// window is judged against it. A copy that drifted would not fail loudly; it would make a skew
// check compare two things measured differently.
std::string utcTimestamp(std::chrono::system_clock::time_point at);

// The same, for now.
std::string utcTimestamp();

}
