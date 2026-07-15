#pragma once

#include <string>
#include <unordered_map>

namespace pz::engined
{

class VendorResolver
{
public:
    void loadOui();

    std::string vendorForMac(const std::string& mac) const;
    std::string vendorForSysObjectId(const std::string& sysObjectId) const;

    std::size_t ouiCount() const
    {
        return m_oui.size();
    }

private:
    std::unordered_map<std::string, std::string> m_oui;
};

}
