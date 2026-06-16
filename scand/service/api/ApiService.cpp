#include "service/api/ApiService.h"

#include "config/Config.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

namespace pz::scand
{

std::map<std::string, ApiCredential> ApiService::loadCredentials() const
{
    std::map<std::string, ApiCredential> out;

    // Per-IP API creds live alongside the SNMPv3 ones in the scan domain
    // (scand.service.scan.api_devices[]) — parallel to v3_devices, set in the same
    // GUI panel and stored in running_config.
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

        ApiCredential c;
        c.vendor    = apiVendorFromString(d.value("vendor", std::string()));
        c.host      = d.value("host",       std::string());
        c.port      = static_cast<uint16_t>(d.value("port", 443));
        c.username  = d.value("username",   std::string());
        c.password  = d.value("password",   std::string());
        c.apiKey    = d.value("api_key",    std::string());
        c.verifyTls = d.value("verify_tls", false);
        c.timeoutMs = d.value("timeout_ms", 5000);

        if (c.vendor == ApiVendor::Unknown)
        {
            LOG_WARN("ApiService: unknown vendor for ip={} — skipping", ip);
            continue;
        }
        out[ip] = std::move(c);
    }

    LOG_INFO("ApiService: loaded {} API device credential(s)", out.size());
    return out;
}

} // namespace pz::scand
