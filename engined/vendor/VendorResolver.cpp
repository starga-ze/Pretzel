#include "vendor/VendorResolver.h"

#include "util/Logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

namespace pz::engined
{

namespace
{

const char* const kOuiCandidatePaths[] = {
    "/etc/pretzel/oui.tsv",
    "shared/data/oui.tsv",
    "./shared/data/oui.tsv",
};

const std::unordered_map<std::string, std::string>& penVendors()
{
    static const std::unordered_map<std::string, std::string> kMap = {
        {"9", "Cisco"},
        {"11", "Hewlett Packard"},
        {"311", "Microsoft"},
        {"343", "Intel"},
        {"674", "Dell"},
        {"1916", "Extreme Networks"},
        {"1991", "Brocade/Foundry"},
        {"2011", "Huawei"},
        {"2435", "Brother"},
        {"2636", "Juniper"},
        {"3375", "F5 Networks"},
        {"4526", "Netgear"},
        {"5951", "Citrix (NetScaler)"},
        {"6027", "Dell (Force10)"},
        {"6876", "VMware"},
        {"8072", "Net-SNMP"},
        {"11863", "Ruckus"},
        {"12356", "Fortinet"},
        {"14179", "Cisco (Wireless)"},
        {"14988", "MikroTik"},
        {"25461", "Palo Alto Networks"},
        {"41112", "Ubiquiti"},
    };
    return kMap;
}

std::string normalizeMac(const std::string& mac)
{
    std::string h;
    for (char c : mac)
    {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            h += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (h.size() == 6)
            break;
    }
    return h;
}

}

void VendorResolver::loadOui()
{
    for (const char* path : kOuiCandidatePaths)
    {
        std::ifstream in(path);
        if (!in.is_open())
            continue;

        std::string line;
        while (std::getline(in, line))
        {
            const auto tab = line.find('\t');
            if (tab == std::string::npos)
                continue;
            std::string prefix = line.substr(0, tab);
            std::string vendor = line.substr(tab + 1);
            if (!prefix.empty() && !vendor.empty())
                m_oui.emplace(std::move(prefix), std::move(vendor));
        }

        LOG_INFO("loaded OUI entries (count={}, path={})", m_oui.size(), path);
        return;
    }

    LOG_WARN("no oui.tsv found — host vendor lookup disabled");
}

std::string VendorResolver::vendorForMac(const std::string& mac) const
{
    const std::string key = normalizeMac(mac);
    if (key.size() != 6)
        return {};
    const auto it = m_oui.find(key);
    return it != m_oui.end() ? it->second : std::string{};
}

std::string VendorResolver::vendorForSysObjectId(const std::string& sysObjectId) const
{
    static const std::string kRoot = "1.3.6.1.4.1.";
    if (sysObjectId.rfind(kRoot, 0) != 0)
        return {};

    const std::string rest = sysObjectId.substr(kRoot.size());
    const auto dot = rest.find('.');
    const std::string pen = (dot == std::string::npos) ? rest : rest.substr(0, dot);

    const auto& m = penVendors();
    const auto it = m.find(pen);
    return it != m.end() ? it->second : std::string{};
}

}
