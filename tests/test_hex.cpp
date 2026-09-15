// Hex rendering and random token minting.
//
// randomHex is the reason this unit exists. Four places minted bearer-grade tokens and three of
// them drew from a seeded Mersenne Twister, whose output reveals its state — mgmtd had already
// learned that for session ids and fixed it there, and the fix never reached the OIDC state, the
// PKCE verifier, the SAML request id or the SSO ticket. Here the secure source is the only source,
// so what these tests pin is not just the format but which generator is behind it.

#include "algorithm/Hex.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>

using namespace pz::algorithm;

namespace
{

std::string hexOf(const std::string& raw, bool upper = false)
{
    return toHex(raw.data(), raw.size(), upper);
}

bool isLowerHex(const std::string& s)
{
    return !s.empty() && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

}

// ── toHex ───────────────────────────────────────────────────────────────────────────────────────

TEST(ToHex, RendersTwoLowerCaseDigitsPerByte)
{
    EXPECT_EQ("00010f10ff", hexOf(std::string("\x00\x01\x0F\x10\xFF", 5)));
    EXPECT_EQ("666f6f", hexOf("foo"));
}

TEST(ToHex, CanRenderUpperCaseForTheOneCallerThatWantsIt)
{
    // Certificate fingerprints are conventionally upper-case; everything pretzel stores is lower.
    EXPECT_EQ("AABBCC", hexOf(std::string("\xAA\xBB\xCC", 3), /*upper=*/true));
    EXPECT_EQ("aabbcc", hexOf(std::string("\xAA\xBB\xCC", 3)));
}

TEST(ToHex, DoesNotSignExtendBytesAboveAscii)
{
    // Read as signed char, 0x80 becomes a negative number and the shift produces "f8" or worse.
    EXPECT_EQ("80ff", hexOf(std::string("\x80\xFF", 2)));
}

TEST(ToHex, OutputIsAlwaysTwiceTheInputLength)
{
    for (std::size_t n = 0; n <= 32; ++n)
        EXPECT_EQ(n * 2, toHex(std::string(n, '\x5A').data(), n).size()) << "length " << n;
}

TEST(ToHex, AnEmptyOrNullInputYieldsAnEmptyString)
{
    EXPECT_EQ("", toHex(nullptr, 0));
    EXPECT_EQ("", toHex(nullptr, 16)) << "a length with no buffer must not be dereferenced";
    EXPECT_EQ("", hexOf(""));
}

// ── randomHex ───────────────────────────────────────────────────────────────────────────────────

TEST(RandomHex, ProducesTwoHexCharactersPerRequestedByte)
{
    for (std::size_t n : {1u, 8u, 16u, 32u, 64u})
    {
        const std::string token = randomHex(n);
        EXPECT_EQ(n * 2, token.size()) << "bytes " << n;
        EXPECT_TRUE(isLowerHex(token)) << token;
    }
}

TEST(RandomHex, ZeroBytesIsAnEmptyToken)
{
    EXPECT_EQ("", randomHex(0));
}

TEST(RandomHex, DoesNotRepeatItself)
{
    // Not a randomness test — that belongs to OpenSSL. This catches the failure that actually
    // happens: a generator left unseeded, or a static buffer returned twice, both of which show up
    // immediately as collisions.
    std::set<std::string> seen;
    for (int i = 0; i < 500; ++i)
        seen.insert(randomHex(16));

    EXPECT_EQ(500u, seen.size());
}

TEST(RandomHex, TokensOfTheSameLengthDifferAcrossTheirWholeSpan)
{
    // A generator that varied only its tail — a counter with a constant prefix, say — would pass a
    // collision check and still be guessable. Every position should vary over enough samples.
    constexpr std::size_t kBytes = 16;
    std::set<char> byPosition[kBytes * 2];

    for (int i = 0; i < 200; ++i)
    {
        const std::string token = randomHex(kBytes);
        ASSERT_EQ(kBytes * 2, token.size());
        for (std::size_t p = 0; p < token.size(); ++p)
            byPosition[p].insert(token[p]);
    }

    for (std::size_t p = 0; p < kBytes * 2; ++p)
        EXPECT_GT(byPosition[p].size(), 8u) << "position " << p << " barely varies";
}
