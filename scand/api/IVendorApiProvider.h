#pragma once

#include "api/ApiTypes.h"
#include "snmp/SnmpTypes.h"

namespace pz::scand
{

class IVendorApiProvider
{
public:
    virtual ~IVendorApiProvider() = default;

    virtual ApiVendor vendor() const = 0;

    virtual bool collect(const ApiCredential& cred, SnmpDevice& dev) = 0;
};

}
