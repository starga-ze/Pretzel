#include "util/Secret.h"

#include "algorithm/Base64.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstdlib>
#include <fstream>
#include <vector>

namespace pz::util::secret
{

namespace
{

constexpr std::size_t kKeyLen = 32;    // AES-256
constexpr std::size_t kNonceLen = 12;  // GCM standard nonce
constexpr std::size_t kTagLen = 16;

std::string configDir()
{
    const char* value = std::getenv("PRETZEL_CONFIG_DIR");
    return (value && *value) ? std::string(value) : std::string("/etc/pretzel");
}

// Read once: the file is root-only and does not change while the daemon runs, so re-reading it
// on every call would only widen the window in which the key sits in memory.
const std::vector<unsigned char>& masterKey()
{
    static const std::vector<unsigned char> key = []
    {
        std::vector<unsigned char> out;
        std::ifstream f(keyPath(), std::ios::binary);
        if (!f.is_open())
            return out;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        if (out.size() != kKeyLen)
            out.clear();   // a short or padded file is a broken install, not a usable key
        return out;
    }();
    return key;
}


}

std::string keyPath()
{
    return configDir() + "/credentials.key";
}

bool available()
{
    return masterKey().size() == kKeyLen;
}

std::optional<std::string> encrypt(const std::string& plaintext)
{
    if (!available())
        return std::nullopt;

    std::vector<unsigned char> nonce(kNonceLen);
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1)
        return std::nullopt;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return std::nullopt;

    std::vector<unsigned char> cipher(plaintext.size());
    std::vector<unsigned char> tag(kTagLen);
    int len = 0, total = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceLen), nullptr) == 1 &&
              EVP_EncryptInit_ex(ctx, nullptr, nullptr, masterKey().data(), nonce.data()) == 1;

    if (ok && !plaintext.empty())
    {
        ok = EVP_EncryptUpdate(ctx, cipher.data(), &len,
                               reinterpret_cast<const unsigned char*>(plaintext.data()),
                               static_cast<int>(plaintext.size())) == 1;
        total = len;
    }
    if (ok)
        ok = EVP_EncryptFinal_ex(ctx, cipher.data() + total, &len) == 1;
    if (ok)
    {
        total += len;
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagLen), tag.data()) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return std::nullopt;

    // nonce ‖ tag ‖ ciphertext — self-contained, so a row carries everything decrypt needs.
    std::vector<unsigned char> blob;
    blob.reserve(kNonceLen + kTagLen + static_cast<std::size_t>(total));
    blob.insert(blob.end(), nonce.begin(), nonce.end());
    blob.insert(blob.end(), tag.begin(), tag.end());
    blob.insert(blob.end(), cipher.begin(), cipher.begin() + total);
    return pz::algorithm::base64Encode(blob);
}

std::optional<std::string> decrypt(const std::string& encoded)
{
    if (!available())
        return std::nullopt;

    // SkipInvalid, not Reject: a sealed blob may have picked up wrapping on its way through a
    // config file or an editor, and the AES-GCM tag is what actually authenticates it.
    std::vector<std::uint8_t> blob;
    pz::algorithm::base64Decode(encoded, blob, pz::algorithm::Base64Strictness::SkipInvalid);
    if (blob.size() < kNonceLen + kTagLen)
        return std::nullopt;

    const unsigned char* nonce = blob.data();
    const unsigned char* tag = blob.data() + kNonceLen;
    const unsigned char* cipher = blob.data() + kNonceLen + kTagLen;
    const int cipherLen = static_cast<int>(blob.size() - kNonceLen - kTagLen);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return std::nullopt;

    std::string out(static_cast<std::size_t>(cipherLen), '\0');
    int len = 0, total = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceLen), nullptr) == 1 &&
              EVP_DecryptInit_ex(ctx, nullptr, nullptr, masterKey().data(), nonce) == 1;

    if (ok && cipherLen > 0)
    {
        ok = EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(&out[0]), &len, cipher, cipherLen) == 1;
        total = len;
    }
    if (ok)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagLen),
                                 const_cast<unsigned char*>(tag)) == 1;
    // Final is where GCM verifies the tag: a wrong key or a modified row fails here rather than
    // yielding plausible-looking garbage.
    if (ok)
        ok = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&out[0]) + total, &len) == 1;

    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return std::nullopt;

    out.resize(static_cast<std::size_t>(total + len));
    return out;
}

}
