// Covers pz::util::secret — the AES-256-GCM credential store that seals device API keys before
// they reach the database.
//
// The property that earns this its own file is authentication, not confidentiality: GCM must
// REFUSE a modified ciphertext rather than return plausible garbage. A silently-corrupted key
// would be handed to a customer firewall as a credential, and the failure would surface as a
// mysterious auth error days later.
//
// This target supplies its own main(): secret::masterKey() caches the key file in a
// function-local static, so PRETZEL_CONFIG_DIR has to be pointed at a fixture before any test
// touches the API. That same caching is why "no key installed" is not covered here — it would
// need a second process. See the note on Unavailable below.

#include "util/Secret.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{

std::string g_configDir;

// Writes a 32-byte key into a private temp dir and points the library at it.
bool installFixtureKey()
{
    char dir[] = "/tmp/pz-secret-test-XXXXXX";
    if (!mkdtemp(dir))
        return false;

    g_configDir = dir;

    const std::string keyPath = g_configDir + "/credentials.key";
    std::ofstream f(keyPath, std::ios::binary);
    if (!f.is_open())
        return false;

    // Fixed bytes rather than random: a failure should be reproducible.
    for (int i = 0; i < 32; ++i)
        f.put(static_cast<char>(i));
    f.close();

    return setenv("PRETZEL_CONFIG_DIR", g_configDir.c_str(), 1) == 0;
}

}

// ── Key discovery ───────────────────────────────────────────────────────────────────────

TEST(SecretKey, IsAvailableWhenTheFixtureKeyIsInstalled)
{
    EXPECT_TRUE(pz::util::secret::available());
}

TEST(SecretKey, PathFollowsTheConfigDirectory)
{
    // PRETZEL_CONFIG_DIR is what lets this run outside /etc/pretzel at all.
    EXPECT_EQ(g_configDir + "/credentials.key", pz::util::secret::keyPath());
}

// ── Round trip ──────────────────────────────────────────────────────────────────────────

TEST(SecretRoundTrip, RecoversThePlaintext)
{
    const std::string key = "LUFRPT1hbGtqc2RmbGtqc2RmCg==";   // shaped like a PAN-OS key

    const auto sealed = pz::util::secret::encrypt(key);
    ASSERT_TRUE(sealed.has_value());

    const auto opened = pz::util::secret::decrypt(*sealed);
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(key, *opened);
}

TEST(SecretRoundTrip, HandlesAnEmptyPlaintext)
{
    const auto sealed = pz::util::secret::encrypt("");
    ASSERT_TRUE(sealed.has_value());

    const auto opened = pz::util::secret::decrypt(*sealed);
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ("", *opened);
}

TEST(SecretRoundTrip, HandlesBinaryAndNonAsciiContent)
{
    const std::string blob("\x00\x01\xff\xfe binary \xed\x95\x9c\xea\xb8\x80", 22);

    const auto sealed = pz::util::secret::encrypt(blob);
    ASSERT_TRUE(sealed.has_value());
    EXPECT_EQ(blob, pz::util::secret::decrypt(*sealed).value_or(""));
}

TEST(SecretRoundTrip, HandlesALongPlaintext)
{
    const std::string big(64 * 1024, 'k');

    const auto sealed = pz::util::secret::encrypt(big);
    ASSERT_TRUE(sealed.has_value());
    EXPECT_EQ(big, pz::util::secret::decrypt(*sealed).value_or(""));
}

TEST(SecretRoundTrip, OutputIsAsciiSoItSurvivesATextColumn)
{
    // The sealed value goes into api_key_state.secret_enc, a TEXT column.
    const auto sealed = pz::util::secret::encrypt("some key");
    ASSERT_TRUE(sealed.has_value());

    for (unsigned char c : *sealed)
        ASSERT_TRUE(c >= 0x20 && c < 0x7f) << "non-printable byte in the sealed value";
}

// ── Nonce reuse ─────────────────────────────────────────────────────────────────────────

TEST(SecretNonce, TheSamePlaintextSealsDifferentlyEachTime)
{
    // A fresh nonce per call. Repeating one under the same key is the classic GCM break, and it
    // would also let anyone reading the table see which two devices share a credential.
    const std::string key = "identical";

    const auto a = pz::util::secret::encrypt(key);
    const auto b = pz::util::secret::encrypt(key);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());

    EXPECT_NE(*a, *b);
    EXPECT_EQ(key, pz::util::secret::decrypt(*a).value_or(""));
    EXPECT_EQ(key, pz::util::secret::decrypt(*b).value_or(""));
}

// ── Tamper detection — the reason GCM was chosen ────────────────────────────────────────

TEST(SecretTamper, RejectsAModifiedCiphertext)
{
    auto sealed = pz::util::secret::encrypt("a real api key");
    ASSERT_TRUE(sealed.has_value());

    // Flip a character near the end, which lands in the ciphertext body.
    std::string corrupted = *sealed;
    corrupted[corrupted.size() - 2] = (corrupted[corrupted.size() - 2] == 'A') ? 'B' : 'A';

    EXPECT_FALSE(pz::util::secret::decrypt(corrupted).has_value())
        << "a modified row must fail, not decrypt to garbage";
}

TEST(SecretTamper, RejectsAModifiedNonce)
{
    auto sealed = pz::util::secret::encrypt("a real api key");
    ASSERT_TRUE(sealed.has_value());

    std::string corrupted = *sealed;
    corrupted[0] = (corrupted[0] == 'A') ? 'B' : 'A';

    EXPECT_FALSE(pz::util::secret::decrypt(corrupted).has_value());
}

TEST(SecretTamper, RejectsATruncatedValue)
{
    auto sealed = pz::util::secret::encrypt("a real api key");
    ASSERT_TRUE(sealed.has_value());

    EXPECT_FALSE(pz::util::secret::decrypt(sealed->substr(0, sealed->size() / 2)).has_value());
}

TEST(SecretTamper, RejectsAValueShorterThanNonceAndTag)
{
    // Below 28 bytes there is not even a header to check — it must be refused by length rather
    // than read past the buffer.
    EXPECT_FALSE(pz::util::secret::decrypt("").has_value());
    EXPECT_FALSE(pz::util::secret::decrypt("AAAA").has_value());
    EXPECT_FALSE(pz::util::secret::decrypt("QUJDREVGRw==").has_value());
}

TEST(SecretTamper, RejectsGarbageThatIsNotEvenBase64)
{
    EXPECT_FALSE(pz::util::secret::decrypt("!!!! not base64 !!!!").has_value());
    EXPECT_FALSE(pz::util::secret::decrypt("plain text that was never sealed").has_value());
}

TEST(SecretTamper, RejectsAValueWithExtraBytesAppended)
{
    auto sealed = pz::util::secret::encrypt("a real api key");
    ASSERT_TRUE(sealed.has_value());

    EXPECT_FALSE(pz::util::secret::decrypt(*sealed + "QUJD").has_value());
}

// ── Unavailable key ─────────────────────────────────────────────────────────────────────
// Not covered here: masterKey() caches in a function-local static, so a "key missing" case
// cannot follow a "key present" one in the same process. Covering it needs a second executable
// that never installs the fixture. Recorded rather than silently skipped.

int main(int argc, char** argv)
{
    if (!installFixtureKey())
    {
        std::fprintf(stderr, "failed to install the fixture key — cannot run\n");
        return 1;
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
