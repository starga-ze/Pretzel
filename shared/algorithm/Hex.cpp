#include "algorithm/Hex.h"

#include <openssl/rand.h>

#include <cstdint>
#include <vector>

namespace pz::algorithm
{

std::string toHex(const void* data, std::size_t len, bool upper)
{
    static const char* kLower = "0123456789abcdef";
    static const char* kUpper = "0123456789ABCDEF";
    const char* tbl = upper ? kUpper : kLower;

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::string out;
    if (data == nullptr)
        return out;

    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i)
    {
        out.push_back(tbl[(bytes[i] >> 4) & 0x0F]);
        out.push_back(tbl[bytes[i] & 0x0F]);
    }
    return out;
}

std::string randomHex(std::size_t nBytes)
{
    if (nBytes == 0)
        return {};

    std::vector<std::uint8_t> buf(nBytes);

    // RAND_bytes rather than a seeded PRNG: the output of a Mersenne Twister reveals its state, so
    // a token minted from one is guessable by anyone who has seen a few of its siblings.
    if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1)
        return {};

    return toHex(buf.data(), buf.size());
}

}
