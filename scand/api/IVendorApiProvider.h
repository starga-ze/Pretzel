#pragma once

#include "api/ApiTypes.h"
#include "snmp/SnmpTypes.h"

namespace pz::scand
{

// Strategy interface for a vendor's management API. One implementation per vendor
// family (PaloAlto today). The engine's API stage looks the provider up by vendor
// in VendorApiRegistry and calls collect() inside a worker thread.
//
// collect() AUGMENTS the device in place — it fills the topology fields the SNMP
// stages couldn't (interfaces / ifTable / arpEntries / lldpNeighbors) and may fill
// identity fields (sysName, etc.) when they're still empty. It must not clobber
// data already gathered over SNMP unless it has something strictly better.
class IVendorApiProvider
{
public:
    virtual ~IVendorApiProvider() = default;

    virtual ApiVendor vendor() const = 0;

    // Returns true if any data was collected. `dev.ip` is set; `cred.host`
    // overrides the endpoint when present, otherwise the device IP is used.
    virtual bool collect(const ApiCredential& cred, SnmpDevice& dev) = 0;
};

} // namespace pz::scand
