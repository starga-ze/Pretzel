// The one base64, replacing five.
//
// Every copy it replaced was load-bearing somewhere it could not be observed easily: the HTTP Basic
// credential collectord sends a Palo Alto cloud API, the sealed blobs in the credential store, the
// SAML AuthnRequest an IdP has to read back, and the PKCE challenge an OIDC provider compares
// against. A byte wrong in any of them is a login or a collection that fails with no clue why, so
// the RFC 4648 vectors are pinned here rather than trusted to a re-read of the bit shifting.

#include "algorithm/Base64.h"

#include <gtest/gtest.h>

#include <numeric>
#include <string>
#include <vector>

using namespace pz::algorithm;

namespace
{

std::string encode(const std::string& in, Base64Alphabet a = Base64Alphabet::Standard, bool pad = true)
{
    return base64Encode(in, a, pad);
}

std::string decodeToString(const std::string& in, Base64Strictness s = Base64Strictness::Reject)
{
    std::vector<std::uint8_t> out;
    if (!base64Decode(in, out, s))
        return "<rejected>";
    return std::string(out.begin(), out.end());
}

}

// ── Encoding ────────────────────────────────────────────────────────────────────────────────────

TEST(Base64Encode, MatchesTheRfc4648TestVectors)
{
    EXPECT_EQ("", encode(""));
    EXPECT_EQ("Zg==", encode("f"));
    EXPECT_EQ("Zm8=", encode("fo"));
    EXPECT_EQ("Zm9v", encode("foo"));
    EXPECT_EQ("Zm9vYg==", encode("foob"));
    EXPECT_EQ("Zm9vYmE=", encode("fooba"));
    EXPECT_EQ("Zm9vYmFy", encode("foobar"));
}

TEST(Base64Encode, PadsToAMultipleOfFourByDefault)
{
    for (std::size_t n = 1; n <= 16; ++n)
        EXPECT_EQ(0u, encode(std::string(n, 'x')).size() % 4) << "input length " << n;
}

TEST(Base64Encode, CanOmitThePadding)
{
    // JWT and PKCE both carry unpadded values; '=' in a URL would have to be escaped.
    EXPECT_EQ("Zg", encode("f", Base64Alphabet::Standard, /*pad=*/false));
    EXPECT_EQ("Zm8", encode("fo", Base64Alphabet::Standard, false));
    EXPECT_EQ("Zm9v", encode("foo", Base64Alphabet::Standard, false)) << "a whole group needs no padding either way";
}

TEST(Base64Encode, TheUrlSafeAlphabetAvoidsEveryCharacterAUrlWouldEscape)
{
    // 0xFB 0xFF encodes to the two indices that differ between the alphabets: 62 and 63.
    const std::string raw("\xFB\xFF", 2);

    EXPECT_EQ("+/8=", encode(raw, Base64Alphabet::Standard));
    EXPECT_EQ("-_8=", encode(raw, Base64Alphabet::UrlSafe));
}

TEST(Base64Encode, UrlSafeUnpaddedIsWhatPkceAndJwtUse)
{
    const std::string raw("\xFB\xFF\xFE", 3);
    const std::string out = base64Encode(raw, Base64Alphabet::UrlSafe, /*pad=*/false);

    EXPECT_EQ(std::string::npos, out.find_first_not_of(
                                     "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"))
        << out;
}

TEST(Base64Encode, HandlesBytesAboveAsciiWithoutSignExtension)
{
    // Read as signed char these go negative and corrupt every later character — the whole credential
    // comes out wrong rather than visibly truncated.
    EXPECT_EQ("//79", encode(std::string("\xFF\xFE\xFD", 3)));
    EXPECT_EQ("AAEC", encode(std::string("\x00\x01\x02", 3)));
}

TEST(Base64Encode, AcceptsANullOrEmptyInputWithoutReadingIt)
{
    EXPECT_EQ("", base64Encode(nullptr, 0));
    EXPECT_EQ("", base64Encode(nullptr, 16)) << "a length with no buffer must not be dereferenced";
    EXPECT_EQ("", base64Encode(std::vector<std::uint8_t>{}));
}

TEST(Base64Encode, TheStringAndVectorOverloadsAgree)
{
    const std::string raw("\x00\x01\xFE\xFF binary", 10);
    const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());

    EXPECT_EQ(base64Encode(raw), base64Encode(bytes));
}

// ── Decoding ────────────────────────────────────────────────────────────────────────────────────

TEST(Base64Decode, ReversesTheRfc4648Vectors)
{
    EXPECT_EQ("", decodeToString(""));
    EXPECT_EQ("f", decodeToString("Zg=="));
    EXPECT_EQ("fo", decodeToString("Zm8="));
    EXPECT_EQ("foo", decodeToString("Zm9v"));
    EXPECT_EQ("foobar", decodeToString("Zm9vYmFy"));
}

TEST(Base64Decode, AcceptsAValueWithItsPaddingLeftOff)
{
    // A JWT segment arrives unpadded; refusing it would refuse every id_token.
    EXPECT_EQ("f", decodeToString("Zg"));
    EXPECT_EQ("fo", decodeToString("Zm8"));
    EXPECT_EQ("foob", decodeToString("Zm9vYg"));
}

TEST(Base64Decode, ReadsEitherAlphabet)
{
    // Which alphabet a value uses is a property of whoever produced it. A JWKS modulus is URL-safe,
    // a sealed blob is standard, and both land in the same decoder.
    EXPECT_EQ(decodeToString("+/8="), decodeToString("-_8="));
}

TEST(Base64Decode, RoundTripsArbitraryBinary)
{
    std::vector<std::uint8_t> raw(256);
    std::iota(raw.begin(), raw.end(), static_cast<std::uint8_t>(0));

    for (auto alphabet : {Base64Alphabet::Standard, Base64Alphabet::UrlSafe})
    {
        for (bool pad : {true, false})
        {
            std::vector<std::uint8_t> back;
            ASSERT_TRUE(base64Decode(base64Encode(raw, alphabet, pad), back));
            EXPECT_EQ(raw, back) << "alphabet " << int(alphabet) << ", pad " << pad;
        }
    }
}

TEST(Base64Decode, StopsAtThePadding)
{
    // Anything after '=' is not data. Reading on would append whatever a caller appended.
    EXPECT_EQ("foo", decodeToString("Zm9v=trailing"));
}

// ── Strictness ──────────────────────────────────────────────────────────────────────────────────

TEST(Base64Decode, RejectModeRefusesACharacterOutsideTheAlphabet)
{
    // The mode for anything that came from outside — a SAMLResponse, a token. An unexpected byte
    // means the input is not what it claims to be, and decoding it anyway invents a value.
    EXPECT_EQ("<rejected>", decodeToString("Zm9v!"));
    EXPECT_EQ("<rejected>", decodeToString("not base64 at all"));
    EXPECT_EQ("<rejected>", decodeToString("Zm9v\nYmFy")) << "even a newline";
}

TEST(Base64Decode, SkipInvalidModeIgnoresWrapping)
{
    // The mode for text that may legitimately carry formatting — a blob that went through an editor
    // or a config file and came back wrapped.
    EXPECT_EQ("foobar", decodeToString("Zm9v\nYmFy", Base64Strictness::SkipInvalid));
    EXPECT_EQ("foobar", decodeToString("Zm9v YmFy", Base64Strictness::SkipInvalid));
    EXPECT_EQ("foobar", decodeToString("  Zm9vYmFy  ", Base64Strictness::SkipInvalid));
}

TEST(Base64Decode, ARejectedDecodeLeavesNothingBehind)
{
    // A caller that ignores the return value must not find a partial result sitting in `out` and
    // mistake it for a whole one.
    std::vector<std::uint8_t> out{1, 2, 3};

    EXPECT_FALSE(base64Decode("Zm9v!", out));
    EXPECT_TRUE(out.empty()) << "the previous contents must be gone too";
}

TEST(Base64Decode, ClearsTheOutputEvenOnSuccess)
{
    std::vector<std::uint8_t> out{9, 9, 9};

    ASSERT_TRUE(base64Decode("Zm9v", out));
    EXPECT_EQ("foo", std::string(out.begin(), out.end())) << "the result must not be appended to what was there";
}
