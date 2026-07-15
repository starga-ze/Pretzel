#pragma once

#include "api/ApiTypes.h"
#include "api/IVendorApiProvider.h"
#include "snmp/SnmpTypes.h"

#include <map>
#include <memory>

namespace pz::scand
{

class VendorApiRegistry
{
public:
    VendorApiRegistry();

    bool collect(const ApiCredential& cred, SnmpDevice& dev);

private:
    std::map<ApiVendor, std::unique_ptr<IVendorApiProvider>> m_providers;
};

}
