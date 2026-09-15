#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pz::algorithm
{

// base64, once. It had been written five times — in Secret, in collectord's ApiUtil, twice in
// authd (standard and URL-safe) and again in mgmtd's SsoController — and the copies had drifted
// apart in the one place it matters: what a decoder does with a character that is not base64.
// Secret's skipped it and kept going; authd's rejected the input. Same name, same signature,
// opposite answers, and nothing said which was intended where.
//
// So the mode is a parameter here rather than a property of whichever copy you happened to call.

enum class Base64Alphabet
{
    Standard,   // '+' and '/' — MIME, HTTP Basic, sealed blobs
    UrlSafe,    // '-' and '_' — JWT, PKCE, anything that goes in a URL without escaping
};

enum class Base64Strictness
{
    // A character outside the alphabet fails the decode. Right for anything that arrived from
    // outside — a SAMLResponse, a token — where unexpected bytes mean the input is not what it
    // claims to be.
    Reject,

    // Characters outside the alphabet are skipped. Right for text that may legitimately carry
    // wrapping — newlines in a PEM-style blob — where rejecting would fail on formatting alone.
    SkipInvalid,
};

// Padding is separable from the alphabet because they vary independently: JWT and PKCE use the
// URL-safe alphabet with no padding, while a URL-safe value stored in a database may keep it.
std::string base64Encode(const void* data, std::size_t len, Base64Alphabet alphabet = Base64Alphabet::Standard,
                         bool pad = true);

std::string base64Encode(const std::string& in, Base64Alphabet alphabet = Base64Alphabet::Standard, bool pad = true);

std::string base64Encode(const std::vector<std::uint8_t>& in, Base64Alphabet alphabet = Base64Alphabet::Standard,
                         bool pad = true);

// Decoding accepts either alphabet regardless of which is asked for — '-' and '_' are read as 62
// and 63 alongside '+' and '/' — because a value's alphabet is a property of who produced it, and
// refusing to read a URL-safe token because the caller named the standard alphabet would be a
// distinction without a purpose. Padding is optional on input either way.
//
// Returns false on a character the strictness mode refuses; `out` is cleared first, so a failed
// decode never leaves the caller holding a partial result it might mistake for a whole one.
bool base64Decode(const std::string& in, std::vector<std::uint8_t>& out,
                  Base64Strictness strictness = Base64Strictness::Reject);

}
