#pragma once

#include "api/IVendorApiProvider.h"

#include <string>

namespace pz::scand
{

// PAN-OS XML API provider. Obtains an API key (keygen, or a pre-shared key from
// config) then issues type=op commands and parses the XML into the SnmpDevice
// model:
//   show interface "all"          -> ifTable (hw) + interfaces (ifnet IPs)
//   show arp all                  -> arpEntries
//   show lldp neighbors all       -> lldpNeighbors
//   show system info              -> sysName/sysDescr (only if still empty)
class PaloAltoApiProvider final : public IVendorApiProvider
{
public:
    ApiVendor vendor() const override { return ApiVendor::PaloAlto; }
    bool collect(const ApiCredential& cred, SnmpDevice& dev) override;

private:
    // Resolve the API key: pre-shared from config, else keygen with user/password.
    // Returns empty on failure.
    std::string resolveApiKey(const ApiCredential& cred, const std::string& host);

    // Run a type=op command and return the raw <response> XML body, or "" on error.
    std::string op(const ApiCredential& cred, const std::string& host,
                   const std::string& apiKey, const std::string& xmlCmd);

    void parseInterfaces(const std::string& xml, SnmpDevice& dev);
    void parseArp(const std::string& xml, SnmpDevice& dev);
    void parseLldp(const std::string& xml, SnmpDevice& dev);
    void parseSystemInfo(const std::string& xml, SnmpDevice& dev);
};

} // namespace pz::scand
