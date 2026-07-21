#include "util/PasswordHash.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace pz::util
{

namespace
{

constexpr char kPrefix[] = "pbkdf2$";
constexpr std::size_t kDigestLen = 32;   // SHA-256
constexpr std::size_t kSaltBytes = 16;

std::string toHex(const unsigned char* data, std::size_t len)
{
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i)
    {
        out.push_back(hex[(data[i] >> 4) & 0xF]);
        out.push_back(hex[data[i] & 0xF]);
    }
    return out;
}

// Length-independent equality. A plain == returns as soon as two bytes differ, which leaks how
// much of a guess was correct; over many attempts that is enough to recover the value.
bool constantTimeEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

std::string pbkdf2Hex(const std::string& password, const std::string& salt, int iterations)
{
    if (iterations <= 0)
        return {};

    unsigned char out[kDigestLen];
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          reinterpret_cast<const unsigned char*>(salt.data()), static_cast<int>(salt.size()),
                          iterations, EVP_sha256(), static_cast<int>(sizeof(out)), out) != 1)
    {
        return {};
    }
    return toHex(out, sizeof(out));
}

// The format written by earlier builds: SHA-256(password || salt), lowercase hex. Reproduced
// here with OpenSSL rather than the hand-rolled implementation this file used to carry — same
// algorithm, same output, one less copy of a primitive to own.
std::string legacySha256Hex(const std::string& password, const std::string& salt)
{
    const std::string input = password + salt;

    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned int len = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &len, EVP_sha256(), nullptr) != 1)
        return {};

    return toHex(digest, len);
}

// Splits "pbkdf2$<iterations>$<hex>". Returns false for anything else, including a legacy value.
bool parseStored(const std::string& stored, int& iterations, std::string& hex)
{
    if (stored.rfind(kPrefix, 0) != 0)
        return false;

    const auto costStart = sizeof(kPrefix) - 1;
    const auto sep = stored.find('$', costStart);
    if (sep == std::string::npos)
        return false;

    const std::string costStr = stored.substr(costStart, sep - costStart);
    if (costStr.empty() || costStr.find_first_not_of("0123456789") != std::string::npos)
        return false;

    iterations = std::atoi(costStr.c_str());
    hex = stored.substr(sep + 1);
    return iterations > 0 && !hex.empty();
}

}

std::string hashPassword(const std::string& password, const std::string& salt)
{
    const std::string hex = pbkdf2Hex(password, salt, kPbkdf2Iterations);
    if (hex.empty())
        return {};

    return std::string(kPrefix) + std::to_string(kPbkdf2Iterations) + "$" + hex;
}

bool verifyPassword(const std::string& password, const std::string& salt, const std::string& stored)
{
    if (stored.empty())
        return false;

    int iterations = 0;
    std::string expected;

    if (parseStored(stored, iterations, expected))
    {
        // Verified at the cost the value was written with, not the current one, so raising
        // kPbkdf2Iterations never locks anyone out.
        const std::string actual = pbkdf2Hex(password, salt, iterations);
        return !actual.empty() && constantTimeEquals(actual, expected);
    }

    const std::string legacy = legacySha256Hex(password, salt);
    return !legacy.empty() && constantTimeEquals(legacy, stored);
}

bool needsRehash(const std::string& stored)
{
    int iterations = 0;
    std::string hex;
    if (!parseStored(stored, iterations, hex))
        return true;   // legacy or malformed

    return iterations < kPbkdf2Iterations;
}

std::string generateSalt()
{
    unsigned char buf[kSaltBytes];
    if (RAND_bytes(buf, static_cast<int>(sizeof(buf))) != 1)
    {
        // Refuse rather than fall back to a predictable source: a guessable salt turns a
        // stolen table back into a rainbow-table problem. Callers treat empty as failure.
        return {};
    }
    return toHex(buf, sizeof(buf));
}

}
