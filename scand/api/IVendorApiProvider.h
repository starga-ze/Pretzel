#pragma once

#include "api/ApiTypes.h"
#include "snmp/SnmpTypes.h"

namespace pz::scand
{

// Strategy interface for a vendor's management API. One implementation per vendor
// family (PaloAlto today). ApiEngine looks the provider up by vendor in
// VendorApiRegistry and calls collect() inside a worker thread.
//
// collect() fills `dev` from scratch — ApiEngine runs this as the device's only
// collection method (no SNMP data to augment), so identity fields (sysName, etc.)
// and topology fields (interfaces / ifTable / arpEntries / lldpNeighbors) all come
// from here.
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
