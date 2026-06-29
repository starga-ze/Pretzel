#pragma once

#include <cstdint>
#include <string>

namespace pz::scand
{

// Vendor HTTP/management-API families. This enum is the extension point: adding a
// new vendor means adding an enum value, a string mapping below, and an
// IVendorApiProvider implementation registered in VendorApiRegistry. Nothing in the
// engine or the SnmpDevice model changes.
enum class ApiVendor
{
    Unknown,
    PaloAlto,   // PAN-OS XML API (keygen + type=op commands)
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
    case ApiVendor::PaloAlto: return "paloalto";
    default:                  return "unknown";
    }
}

// Per-IP vendor-API credentials. A device whose scan method is "api" (chosen in
// the GUI, mutually exclusive with v2c/v3) is queried entirely over its vendor
// HTTP API by ApiEngine — independent of and parallel to SnmpEngine, not a
// fallback stage after SNMP. PAN-OS, for example, answers sysName/ifTable over
// SNMPv3 but returns "No Such Object" for the ip-MIB address tables; a PAN-OS
// device that needs those should be registered under the api method instead.
//
// Set in the GUI (settings -> SNMP, method "API") and stored as
// scand.service.scan.api_devices[].
struct ApiCredential
{
    ApiVendor   vendor{ApiVendor::Unknown};
    std::string host;            // API endpoint; falls back to the device IP if empty
    uint16_t    port{443};
    std::string username;        // used for keygen when apiKey is empty
    std::string password;
    std::string apiKey;          // pre-shared key; if set, keygen is skipped
    bool        verifyTls{false};// self-signed mgmt certs are the norm on appliances
    int         timeoutMs{5000};
};

} // namespace pz::scand
