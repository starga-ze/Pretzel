#pragma once

#include "api/IVendorApiProvider.h"

#include <string>

namespace pz::scand
{

class PaloAltoApiProvider final : public IVendorApiProvider
{
public:
    ApiVendor vendor() const override
    {
        return ApiVendor::PaloAlto;
    }
    bool collect(const ApiCredential& cred, SnmpDevice& dev) override;

private:
    std::string resolveApiKey(const ApiCredential& cred, const std::string& host);

    std::string op(const ApiCredential& cred, const std::string& host, const std::string& apiKey,
                   const std::string& xmlCmd);

    void parseInterfaces(const std::string& xml, SnmpDevice& dev);
    void parseArp(const std::string& xml, SnmpDevice& dev);
    void parseLldp(const std::string& xml, SnmpDevice& dev);
    void parseSystemInfo(const std::string& xml, SnmpDevice& dev);
};

}
