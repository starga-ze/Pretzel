#include "api/VendorApiRegistry.h"
#include "api/PaloAltoApiProvider.h"

#include "util/Logger.h"

namespace pz::scand
{

VendorApiRegistry::VendorApiRegistry()
{
    // Register one provider per supported vendor. New vendors plug in here.
    m_providers[ApiVendor::PaloAlto] = std::make_unique<PaloAltoApiProvider>();
}

bool VendorApiRegistry::collect(const ApiCredential& cred, SnmpDevice& dev)
{
    const auto it = m_providers.find(cred.vendor);
    if (it == m_providers.end())
    {
        LOG_DEBUG("VendorApiRegistry: no provider for vendor={} ip={}",
                  apiVendorToString(cred.vendor), dev.ip);
        return false;
    }
    return it->second->collect(cred, dev);
}

} // namespace pz::scand
