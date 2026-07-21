// Covers pz::util::PasswordHash — local-account credential storage.
//
// This is authentication code with no second chance: a mistake here either locks every operator
// out (legacy hashes stop verifying) or silently weakens storage (the KDF is not doing what it
// claims). Both are invisible until they matter, so the properties are pinned here rather than
// left to inspection.

#include "util/PasswordHash.h"

#include <gtest/gtest.h>

#include <string>

using namespace pz::util;

namespace
{

// Published PBKDF2-HMAC-SHA256 vectors for P="password", S="salt", dkLen=32.
constexpr const char* kVectorC1 = "pbkdf2$1$120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b";
constexpr const char* kVectorC4096 = "pbkdf2$4096$c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a";

// sha256("password" + "salt") — exactly what builds before the PBKDF2 change wrote.
constexpr const char* kLegacyHash = "7a37b85c8918eac19a9089c0fa5a2ab4dce3f90528dcdeec108b23ddf3607b99";

}

// ── The KDF is really PBKDF2-HMAC-SHA256 ────────────────────────────────────────────────
// These pin the algorithm itself: were the implementation swapped or misconfigured, stored
// credentials would keep verifying against each other and nothing else would notice.

TEST(PasswordHashKdf, MatchesPublishedVectors)
{
    EXPECT_TRUE(verifyPassword("password", "salt", kVectorC1));
    EXPECT_TRUE(verifyPassword("password", "salt", kVectorC4096));
}

TEST(PasswordHashKdf, RejectsANearMiss)
{
    EXPECT_FALSE(verifyPassword("Password", "salt", kVectorC1));
    EXPECT_FALSE(verifyPassword("password ", "salt", kVectorC1));
}

TEST(PasswordHashKdf, HonoursTheCostStoredInTheValue)
{
    // Verified at the cost recorded inside the value, not kPbkdf2Iterations — otherwise raising
    // the cost would lock out every existing account at once.
    ASSERT_NE(1, kPbkdf2Iterations);
    EXPECT_TRUE(verifyPassword("password", "salt", kVectorC1));
}

// ── Upgrade path from the pre-PBKDF2 format ─────────────────────────────────────────────

TEST(PasswordHashLegacy, OldSha256HashStillVerifies)
{
    // If this fails, every already-deployed operator is locked out.
    EXPECT_TRUE(verifyPassword("password", "salt", kLegacyHash));
}

TEST(PasswordHashLegacy, OldHashStillRejectsAWrongPassword)
{
    EXPECT_FALSE(verifyPassword("wrong", "salt", kLegacyHash));
}

TEST(PasswordHashLegacy, OldHashIsFlaggedForRehash)
{
    EXPECT_TRUE(needsRehash(kLegacyHash));
}

TEST(PasswordHashLegacy, AHashBelowTheCurrentCostIsFlaggedForRehash)
{
    EXPECT_TRUE(needsRehash("pbkdf2$1000$aabb"));
}

// ── Salt ────────────────────────────────────────────────────────────────────────────────

TEST(PasswordHashSalt, IsSixteenBytesHexEncoded)
{
    EXPECT_EQ(32u, generateSalt().size());
}

TEST(PasswordHashSalt, IsNotDegenerate)
{
    const std::string salt = generateSalt();
    EXPECT_NE(std::string(32, '0'), salt);
    EXPECT_FALSE(salt.empty());
}

TEST(PasswordHashSalt, DiffersBetweenCalls)
{
    EXPECT_NE(generateSalt(), generateSalt());
}

// ── Round trip ──────────────────────────────────────────────────────────────────────────
// A fixture so each case starts from a freshly generated credential rather than sharing one.

class PasswordHashRoundTrip : public ::testing::Test
{
protected:
    void SetUp() override
    {
        salt = generateSalt();
        ASSERT_FALSE(salt.empty()) << "CSPRNG unavailable — the rest of this case is meaningless";

        stored = hashPassword("hunter2", salt);
        ASSERT_FALSE(stored.empty()) << "hashing failed";
    }

    std::string salt;
    std::string stored;
};

TEST_F(PasswordHashRoundTrip, HashIsSelfDescribing)
{
    EXPECT_EQ(0u, stored.rfind("pbkdf2$", 0)) << "stored = " << stored;
}

TEST_F(PasswordHashRoundTrip, CorrectPasswordVerifies)
{
    EXPECT_TRUE(verifyPassword("hunter2", salt, stored));
}

TEST_F(PasswordHashRoundTrip, WrongPasswordIsRejected)
{
    EXPECT_FALSE(verifyPassword("hunter3", salt, stored));
}

TEST_F(PasswordHashRoundTrip, RightPasswordWithTheWrongSaltIsRejected)
{
    EXPECT_FALSE(verifyPassword("hunter2", generateSalt(), stored));
}

TEST_F(PasswordHashRoundTrip, FreshHashNeedsNoRehash)
{
    EXPECT_FALSE(needsRehash(stored));
}

// ── Malformed input is refused, never accepted by accident ──────────────────────────────

class PasswordHashMalformed : public ::testing::Test
{
protected:
    void SetUp() override
    {
        salt = generateSalt();
        stored = hashPassword("hunter2", salt);
    }

    std::string salt;
    std::string stored;
};

TEST_F(PasswordHashMalformed, EmptyStoredValueIsRejected)
{
    EXPECT_FALSE(verifyPassword("x", salt, ""));
}

TEST_F(PasswordHashMalformed, EmptyPasswordDoesNotMatchARealHash)
{
    EXPECT_FALSE(verifyPassword("", salt, stored));
}

TEST_F(PasswordHashMalformed, TruncatedFormatIsRejected)
{
    EXPECT_FALSE(verifyPassword("x", salt, "pbkdf2$$"));
    EXPECT_FALSE(verifyPassword("x", salt, "pbkdf2$"));
}

TEST_F(PasswordHashMalformed, NonNumericCostIsRejected)
{
    EXPECT_FALSE(verifyPassword("x", salt, "pbkdf2$abc$aabb"));
    EXPECT_TRUE(needsRehash("pbkdf2$abc$aabb"));
}
