#pragma once

#include <cstdint>
#include <string>

namespace pz::scand
{

enum class ApiVendor
{
    Unknown,
    PaloAlto,
};

inline ApiVendor apiVendorFromString(const std::string& s)
{
    if (s == "paloalto" || s == "panos" || s == "pan-os")
        return ApiVendor::PaloAlto;
    return ApiVendor::Unknown;
}

inline const char* apiVendorToString(ApiVendor v)
{
    switch (v)
    {
    case ApiVendor::PaloAlto:
        return "paloalto";
    default:
        return "unknown";
    }
}

struct ApiCredential
{
    ApiVendor vendor{ApiVendor::Unknown};
    std::string host;
    uint16_t port{443};
    std::string username;
    std::string password;
    std::string apiKey;
    bool verifyTls{false};
    int timeoutMs{5000};
};

}
