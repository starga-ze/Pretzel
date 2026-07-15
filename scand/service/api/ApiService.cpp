#include "service/api/ApiService.h"

#include "config/Config.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::scand
{

std::map<std::string, ApiCredential> ApiService::loadCredentials() const
{
    std::map<std::string, ApiCredential> out;

    const auto& scan = pz::config::Config::serviceSection("scand", "scan");
    if (!scan.contains("api_devices") || !scan["api_devices"].is_array())
        return out;

    for (const auto& d : scan["api_devices"])
    {
        if (!d.is_object())
            continue;

        const std::string ip = d.value("ip", std::string());
        if (ip.empty())
            continue;

        if (!d.value("enabled", true))
            continue;

        ApiCredential c;
        c.vendor = apiVendorFromString(d.value("vendor", std::string()));
        c.host = d.value("host", std::string());
        c.port = static_cast<uint16_t>(d.value("port", 443));
        c.username = d.value("username", std::string());
        c.password = d.value("password", std::string());
        c.apiKey = d.value("api_key", std::string());
        c.verifyTls = d.value("verify_tls", false);
        c.timeoutMs = d.value("timeout_ms", 5000);

        if (c.vendor == ApiVendor::Unknown)
        {
            LOG_WARN("unknown API vendor — skipping (ip={})", ip);
            continue;
        }
        out[ip] = std::move(c);
    }

    LOG_DEBUG("loaded API device credentials (count={})", out.size());
    return out;
}

}
