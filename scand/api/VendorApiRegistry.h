#pragma once

#include "api/ApiTypes.h"
#include "api/IVendorApiProvider.h"
#include "snmp/SnmpTypes.h"

#include <map>
#include <memory>

namespace pz::scand
{

// Owns one provider per vendor and dispatches collect() by ApiCredential::vendor.
// Construct once (as a member of ApiEngine) and call collect() from worker
// threads. Providers must be stateless / thread-safe across concurrent collect()
// calls — the current PAN-OS provider holds no per-call state.
class VendorApiRegistry
{
public:
    VendorApiRegistry();

    // Dispatches to the provider for cred.vendor. Returns false (no-op) when the
    // vendor has no registered provider. Augments `dev` in place.
    bool collect(const ApiCredential& cred, SnmpDevice& dev);

private:
    std::map<ApiVendor, std::unique_ptr<IVendorApiProvider>> m_providers;
};

} // namespace pz::scand
