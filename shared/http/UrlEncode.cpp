#include "http/UrlEncode.h"

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

}
