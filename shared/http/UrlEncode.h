#pragma once

#include <string>

namespace pz::http
{

// Percent-encode one query-string value. PAN-OS takes credentials and XML API commands as query
// parameters, and mgmtd reuses this to render the request line it logs — so it lives in shared,
// independent of the outbound client (which is collectord's alone).
std::string urlEncode(const std::string& raw);

// The inverse, for a query-string value. '+' decodes to a space — that is form encoding, which is
// what a query string is, and it is the one place percent-decoding is NOT simply the reverse of
// urlEncode (which escapes a space as %20 and never emits '+'). A path segment must not be decoded
// with this: there '+' is a literal.
//
// A malformed escape — a '%' with fewer than two hex digits after it — is left as typed rather than
// dropped, so a value the operator can see in the address bar is the value the handler receives.
std::string urlDecode(const std::string& encoded);

}
