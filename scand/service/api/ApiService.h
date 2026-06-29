#pragma once

#include "api/ApiTypes.h"

#include <map>
#include <string>

namespace pz::scand
{

// Service-layer seam for the vendor-API scan method. Owns the API-specific
// configuration knowledge (scand.service.scan.api_devices[]) and keeps it out of
// the SNMP-focused ScanService.
//
// It deliberately has no IPC Event/Action plumbing of its own: ScanService calls
// loadCredentials() when it splits an incoming ScanRequest by method, then hands
// the result to ApiEngine via TxRouter — mirroring how it builds SnmpScanConfig for
// SnmpEngine. Future API-only operations (on-demand refresh, per-vendor health
// checks) would attach here.
class ApiService
{
public:
    ApiService() = default;

    // Parse scand.service.scan.api_devices[] into a per-IP credential map. Returns
    // an empty map when the section is absent or malformed (fails soft).
    std::map<std::string, ApiCredential> loadCredentials() const;
};

} // namespace pz::scand
