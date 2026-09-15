#include "http/UrlEncode.h"

#include <cstddef>

namespace pz::http
{

std::string urlEncode(const std::string& raw)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size() * 3);
    for (unsigned char c : raw)
    {
        // Spelled out rather than std::isalnum: that one answers per the active C locale, and the
        // RFC 3986 unreserved set is fixed. A locale that classed extra bytes as alphanumeric
        // would leave them unescaped in a query string.
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved)
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::string urlDecode(const std::string& encoded)
{
    auto hexVal = [](unsigned char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    std::string out;
    out.reserve(encoded.size());

    for (std::size_t i = 0; i < encoded.size(); ++i)
    {
        if (encoded[i] == '+')
        {
            out.push_back(' ');
            continue;
        }

        if (encoded[i] == '%' && i + 2 < encoded.size())
        {
            const int hi = hexVal(static_cast<unsigned char>(encoded[i + 1]));
            const int lo = hexVal(static_cast<unsigned char>(encoded[i + 2]));
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }

        out.push_back(encoded[i]);
    }

    return out;
}

}
