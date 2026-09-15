#include "algorithm/Base64.h"

namespace pz::algorithm
{

namespace
{

const char* alphabetOf(Base64Alphabet a)
{
    static const char kStandard[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const char kUrlSafe[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    return a == Base64Alphabet::UrlSafe ? kUrlSafe : kStandard;
}

// -1 for anything that is not a base64 digit. Both alphabets are accepted; see the header.
int valueOf(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+' || c == '-')
        return 62;
    if (c == '/' || c == '_')
        return 63;
    return -1;
}

}

std::string base64Encode(const void* data, std::size_t len, Base64Alphabet alphabet, bool pad)
{
    std::string out;
    if (data == nullptr || len == 0)
        return out;

    const char* tbl = alphabetOf(alphabet);
    const auto* d = static_cast<const std::uint8_t*>(data);

    out.reserve((len + 2) / 3 * 4);

    std::size_t i = 0;
    for (; i + 2 < len; i += 3)
    {
        const std::uint32_t n = (static_cast<std::uint32_t>(d[i]) << 16) |
                                (static_cast<std::uint32_t>(d[i + 1]) << 8) | static_cast<std::uint32_t>(d[i + 2]);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back(tbl[n & 63]);
    }

    // The tail: one or two bytes left over become two or three characters, plus padding to a
    // multiple of four when the caller wants it.
    if (i < len)
    {
        std::uint32_t n = static_cast<std::uint32_t>(d[i]) << 16;
        const bool twoLeft = (i + 1 < len);
        if (twoLeft)
            n |= static_cast<std::uint32_t>(d[i + 1]) << 8;

        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        if (twoLeft)
            out.push_back(tbl[(n >> 6) & 63]);

        if (pad)
        {
            if (!twoLeft)
                out.push_back('=');
            out.push_back('=');
        }
    }

    return out;
}

std::string base64Encode(const std::string& in, Base64Alphabet alphabet, bool pad)
{
    return base64Encode(in.data(), in.size(), alphabet, pad);
}

std::string base64Encode(const std::vector<std::uint8_t>& in, Base64Alphabet alphabet, bool pad)
{
    return base64Encode(in.data(), in.size(), alphabet, pad);
}

bool base64Decode(const std::string& in, std::vector<std::uint8_t>& out, Base64Strictness strictness)
{
    out.clear();
    out.reserve(in.size() * 3 / 4);

    int buf = 0;
    int bits = 0;
    for (const char c : in)
    {
        // Padding ends the value; anything after it is not data.
        if (c == '=')
            break;

        const int v = valueOf(c);
        if (v < 0)
        {
            if (strictness == Base64Strictness::Reject)
            {
                out.clear();
                return false;
            }
            continue;
        }

        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
        }
    }

    return true;
}

}
