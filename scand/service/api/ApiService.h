#pragma once

#include "api/ApiTypes.h"

#include <map>
#include <string>

namespace pz::scand
{

// Service-layer seam for the vendor-API collection stage. Owns the API-specific
// configuration knowledge (scand.service.scan.api_devices[]) and keeps it out of
// the SNMP-focused ScanService.
//
// It deliberately has no IPC Event/Action plumbing: API collection is not a
// separate request from engined, it's the terminal stage of the v2c -> v3 -> API
// chain that runs inside one SNMP scan cycle. ScanService folds the credentials
// this returns into the SnmpScanConfig; the engine executes the stage in a worker
// thread via VendorApiRegistry. Future API-only operations (on-demand refresh,
// per-vendor health checks) would attach here.
class ApiService
{
public:
    ApiService() = default;

    // Parse scand.service.scan.api_devices[] into a per-IP credential map. Returns
    // an empty map when the section is absent or malformed (fails soft).
    std::map<std::string, ApiCredential> loadCredentials() const;
};

} // namespace pz::scand
