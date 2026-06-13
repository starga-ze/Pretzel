#include "util/PasswordHash.h"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>

namespace pz::util
{

namespace
{

// Self-contained SHA-256 (FIPS 180-4). Deliberately NOT OpenSSL: pz-shared is linked
// into every daemon and also pulls in libpq, which uses the *dynamic* system OpenSSL.
// Linking the project's *static* OpenSSL here too put two OpenSSL copies in one
// process and crashed libpq's OPENSSL_init_ssl on DB connect. A dependency-free hash
// avoids that collision entirely.
class Sha256
{
public:
    Sha256() { reset(); }

    void update(const std::uint8_t* data, std::size_t len)
    {
        for (std::size_t i = 0; i < len; ++i)
        {
            m_buf[m_bufLen++] = data[i];
            if (m_bufLen == 64)
            {
                transform(m_buf);
                m_bitLen += 512;
                m_bufLen = 0;
            }
        }
    }

    void finish(std::uint8_t out[32])
    {
        const std::uint64_t totalBits = m_bitLen + static_cast<std::uint64_t>(m_bufLen) * 8;

        m_buf[m_bufLen++] = 0x80;
        if (m_bufLen > 56)
        {
            while (m_bufLen < 64) m_buf[m_bufLen++] = 0;
            transform(m_buf);
            m_bufLen = 0;
        }
        while (m_bufLen < 56) m_buf[m_bufLen++] = 0;
        for (int i = 7; i >= 0; --i)
            m_buf[m_bufLen++] = static_cast<std::uint8_t>(totalBits >> (i * 8));
        transform(m_buf);

        for (int i = 0; i < 8; ++i)
        {
            out[i * 4 + 0] = static_cast<std::uint8_t>(m_h[i] >> 24);
            out[i * 4 + 1] = static_cast<std::uint8_t>(m_h[i] >> 16);
            out[i * 4 + 2] = static_cast<std::uint8_t>(m_h[i] >> 8);
            out[i * 4 + 3] = static_cast<std::uint8_t>(m_h[i]);
        }
    }

private:
    static std::uint32_t rotr(std::uint32_t x, std::uint32_t n)
    {
        return (x >> n) | (x << (32 - n));
    }

    void reset()
    {
        m_bufLen = 0;
        m_bitLen = 0;
        static const std::uint32_t kInit[8] = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        std::memcpy(m_h, kInit, sizeof(kInit));
    }

    void transform(const std::uint8_t* p)
    {
        static const std::uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};

        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(p[i * 4 + 3]));
        for (int i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
        std::uint32_t e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];

        for (int i = 0; i < 64; ++i)
        {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }

        m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d;
        m_h[4] += e; m_h[5] += f; m_h[6] += g; m_h[7] += h;
    }

    std::uint32_t m_h[8];
    std::uint8_t  m_buf[64];
    std::size_t   m_bufLen;
    std::uint64_t m_bitLen;
};

} // namespace

std::string hashSha256(const std::string& password, const std::string& salt)
{
    const std::string input = password + salt;

    std::uint8_t digest[32];
    Sha256 sha;
    sha.update(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
    sha.finish(digest);

    std::ostringstream oss;
    for (std::uint8_t byte : digest)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }

    return oss.str();
}

std::string generateSalt()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << dist(gen) << dist(gen);
    return oss.str();
}

} // namespace pz::util
